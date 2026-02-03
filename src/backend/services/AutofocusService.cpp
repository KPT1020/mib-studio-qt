#include "backend/services/AutofocusService.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
#include <Coremor/XMT_DLL_SER.h>

#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>

namespace backend::services {

namespace {
    // Plausible voltage range for XMT nanopositioner (V). Used to validate probe response.
    constexpr double PROBE_VOLTAGE_MIN = 0.0;
    constexpr double PROBE_VOLTAGE_MAX = 250.0;
} // namespace

AutofocusService::AutofocusService() = default;

AutofocusService::~AutofocusService() {
    disconnect();
}

bool AutofocusService::connect(int comPort, int baudRate, unsigned char deviceAddress) {
    if (connected_.load()) {
        disconnect();
    }

    comPort_ = comPort;
    baudRate_ = baudRate;
    deviceAddress_ = deviceAddress;

    int result = OpenComConnectRS232(comPort, baudRate);
    if (result == 0) {
        SPDLOG_ERROR("AutofocusService: Failed to open COM port {}", comPort);
        if (statusCallback_) {
            statusCallback_("Failed to open COM port " + std::to_string(comPort));
        }
        connected_.store(false);
        return false;
    }

    SPDLOG_INFO("AutofocusService: COM port {} opened successfully", comPort);
    connected_.store(true);

    // Initialize voltage
    {
        std::scoped_lock cfgLock(configMutex_);
        double initialVoltage = config_.initialVoltage;
        XMT_COMMAND_SinglePoint(deviceAddress_, 0, 0, 0, initialVoltage);
        currentVoltage_.store(initialVoltage);
    }

    // Start control thread
    if (!running_.load()) {
        running_.store(true);
        controlThread_ = std::thread(&AutofocusService::controlLoop, this);
    }

    if (statusCallback_) {
        statusCallback_("Connected to nanopositioner on COM" + std::to_string(comPort));
    }

    return true;
}

void AutofocusService::disconnect() {
    if (!connected_.load()) {
        return;
    }

    // Stop control thread
    if (running_.load()) {
        running_.store(false);
        if (controlThread_.joinable()) {
            controlThread_.join();
        }
    }

    // Set safe shutdown voltage
    {
        std::scoped_lock cfgLock(configMutex_);
        double safeVoltage = config_.safeShutdownVoltage;
        if (connected_.load()) {
            XMT_COMMAND_SinglePoint(deviceAddress_, 0, 0, 0, safeVoltage);
        }
    }

    // Close COM port
    CloseSer();
    connected_.store(false);

    // Clear buffers
    {
        std::scoped_lock ringLock(ringRatioMutex_);
        ringRatioBuffer_.clear();
        ringRatioTimestamps_.clear();
        ringRatioSequence_.store(0);
        lastRingRatioTimestampNs_.store(0);
    }

    SPDLOG_INFO("AutofocusService: Disconnected from nanopositioner");
    if (statusCallback_) {
        statusCallback_("Disconnected from nanopositioner");
    }
}

bool AutofocusService::probeComPort(int comPort, int baudRate, unsigned char deviceAddress) {
    // Caller must not be connected (SDK uses a single global COM handle).
    int result = OpenComConnectRS232(comPort, baudRate);
    if (result == 0) {
        return false;
    }
    double val = XMT_COMMAND_ReadData(deviceAddress, 5, 0, 0);
    CloseSer();
    bool plausible = std::isfinite(val) && val >= PROBE_VOLTAGE_MIN && val <= PROBE_VOLTAGE_MAX;
    if (plausible) {
        SPDLOG_DEBUG("AutofocusService: COM{} probe OK (read {:.2f} V)", comPort, val);
    }
    return plausible;
}

void AutofocusService::setEnabled(bool enabled) {
    enabled_.store(enabled);
    SPDLOG_INFO("AutofocusService: Autofocus {}", enabled ? "enabled" : "disabled");
    if (statusCallback_) {
        statusCallback_(enabled ? "Autofocus enabled" : "Autofocus disabled");
    }
}

void AutofocusService::increaseVoltage() {
    if (!connected_.load()) {
        return;
    }
    increaseVoltageRequest_.store(true);
}

void AutofocusService::decreaseVoltage() {
    if (!connected_.load()) {
        return;
    }
    decreaseVoltageRequest_.store(true);
}

void AutofocusService::setConfig(const Config& config) {
    std::scoped_lock lock(configMutex_);
    config_ = config;
}

AutofocusService::Config AutofocusService::getConfig() const {
    std::scoped_lock lock(configMutex_);
    return config_;
}

void AutofocusService::onRingRatio(double ringRatio, int64_t timestampNs) {
    // Always accept samples to compute statistics, even if not connected
    if (ringRatio <= 0.0) {
        return;
    }

    std::scoped_lock lock(ringRatioMutex_);
    
    // Add to buffer
    ringRatioBuffer_.push_back(ringRatio);
    ringRatioTimestamps_.push_back(timestampNs);
    
    // Trim buffer if too large
    if (ringRatioBuffer_.size() > MAX_BUFFER_SIZE) {
        ringRatioBuffer_.pop_front();
        ringRatioTimestamps_.pop_front();
    }
    
    // Update sequence and timestamp
    ringRatioSequence_.fetch_add(1, std::memory_order_relaxed);
    lastRingRatioTimestampNs_.store(timestampNs, std::memory_order_relaxed);
    
    // Update statistics
    updateStatistics();
}

void AutofocusService::updateStatistics() {
    if (ringRatioBuffer_.empty()) {
        medianRingRatio_.store(0.0);
        averageRingRatio_.store(0.0);
        minRingRatio_.store(0.0);
        maxRingRatio_.store(0.0);
        return;
    }

    std::vector<double> sorted = std::vector<double>(ringRatioBuffer_.begin(), ringRatioBuffer_.end());
    std::sort(sorted.begin(), sorted.end());

    double median = calculateMedian(sorted);
    double sum = 0.0;
    double minVal = sorted.front();
    double maxVal = sorted.back();

    for (double val : sorted) {
        sum += val;
    }
    double avg = sum / sorted.size();

    medianRingRatio_.store(median);
    averageRingRatio_.store(avg);
    minRingRatio_.store(minVal);
    maxRingRatio_.store(maxVal);
}

double AutofocusService::calculateMedian(const std::vector<double>& sorted) const {
    if (sorted.empty()) return 0.0;
    size_t n = sorted.size();
    if (n % 2 == 0) {
        return (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0;
    } else {
        return sorted[n / 2];
    }
}

void AutofocusService::setStatusCallback(StatusCallback callback) {
    std::scoped_lock lock(callbackMutex_);
    statusCallback_ = std::move(callback);
}

void AutofocusService::controlLoop() {
    SPDLOG_INFO("AutofocusService: Control loop started");

    while (running_.load()) {
        if (!connected_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // Handle manual voltage control requests
        {
            std::scoped_lock controlLock(controlMutex_);
            Config cfg;
            {
                std::scoped_lock cfgLock(configMutex_);
                cfg = config_;
            }

            if (increaseVoltageRequest_.load()) {
                double newVoltage = std::min(currentVoltage_.load() + cfg.manualVoltageStep, cfg.maxVoltage);
                XMT_COMMAND_SinglePoint(deviceAddress_, 0, 0, 0, newVoltage);
                currentVoltage_.store(newVoltage);
                SPDLOG_DEBUG("AutofocusService: Manual voltage increased to {}V", newVoltage);
                increaseVoltageRequest_.store(false);
                if (statusCallback_) {
                    statusCallback_("Voltage: " + std::to_string(newVoltage) + "V");
                }
            }

            if (decreaseVoltageRequest_.load()) {
                double newVoltage = std::max(currentVoltage_.load() - cfg.manualVoltageStep, cfg.minVoltage);
                XMT_COMMAND_SinglePoint(deviceAddress_, 0, 0, 0, newVoltage);
                currentVoltage_.store(newVoltage);
                SPDLOG_DEBUG("AutofocusService: Manual voltage decreased to {}V", newVoltage);
                decreaseVoltageRequest_.store(false);
                if (statusCallback_) {
                    statusCallback_("Voltage: " + std::to_string(newVoltage) + "V");
                }
            }
        }

        // Run automatic control if enabled
        if (enabled_.load() && connected_.load()) {
            Config cfg;
            {
                std::scoped_lock cfgLock(configMutex_);
                cfg = config_;
            }

            // Update current voltage from device
            double currentVolt = XMT_COMMAND_ReadData(deviceAddress_, 5, 0, 0);
            currentVoltage_.store(currentVolt);

            // Get median ring ratio
            double medianRingRatio = medianRingRatio_.load();

            // Check freshness and sample requirements
            uint64_t currentSequence = ringRatioSequence_.load(std::memory_order_relaxed);
            int64_t lastTsNs = lastRingRatioTimestampNs_.load(std::memory_order_relaxed);
            int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now().time_since_epoch())
                                .count();
            bool freshTimestamp = (lastTsNs > 0) && 
                                  (nowNs - lastTsNs <= static_cast<int64_t>(cfg.ringRatioStaleMs) * 1000000LL);
            bool hasNewSample = (currentSequence != lastAppliedSequence_);
            uint64_t samplesSinceStep = currentSequence - lastAppliedSequence_;
            bool hasEnoughSamples = samplesSinceStep >= static_cast<uint64_t>(cfg.minSamplesPerStep);

            // Only perform autofocus control if we have valid data
            if (medianRingRatio > 0.0 && freshTimestamp && 
                (!cfg.requireNewSamplePerStep || hasNewSample) && hasEnoughSamples) {
                
                double deviation = medianRingRatio - cfg.focusSetpoint;
                bool inAcceptableRange = std::abs(deviation) <= cfg.focusRange;

                double newVoltage = currentVoltage_.load();

                if (!inAcceptableRange) {
                    // Outside acceptable range, make larger adjustments
                    if ((deviation < 0 && cfg.focusDirection) || (deviation > 0 && !cfg.focusDirection)) {
                        // Need to increase voltage
                        newVoltage = std::min(newVoltage + cfg.voltageStep, cfg.maxVoltage);
                    } else {
                        // Need to decrease voltage
                        newVoltage = std::max(newVoltage - cfg.voltageStep, cfg.minVoltage);
                    }
                } else {
                    // Within acceptable range, make fine adjustments
                    if (std::abs(deviation) > cfg.focusRange / 2.0) {
                        // Fine adjustment to get closer to exact setpoint
                        if ((deviation < 0 && cfg.focusDirection) || (deviation > 0 && !cfg.focusDirection)) {
                            newVoltage = std::min(newVoltage + cfg.fineVoltageStep, cfg.maxVoltage);
                        } else {
                            newVoltage = std::max(newVoltage - cfg.fineVoltageStep, cfg.minVoltage);
                        }
                    }
                }

                // Apply the new voltage if it changed
                if (std::abs(newVoltage - currentVoltage_.load()) > 0.01) {
                    XMT_COMMAND_SinglePoint(deviceAddress_, 0, 0, 0, newVoltage);
                    currentVoltage_.store(newVoltage);
                    lastAppliedSequence_ = currentSequence;

                    // Clear buffer after a step to ensure next statistics are based on post-step samples
                    {
                        std::scoped_lock ringLock(ringRatioMutex_);
                        ringRatioBuffer_.clear();
                        ringRatioTimestamps_.clear();
                        ringRatioSequence_.store(0);
                        lastRingRatioTimestampNs_.store(0);
                        updateStatistics(); // Update statistics to reflect empty buffer
                    }

                    SPDLOG_DEBUG("AutofocusService: Adjusted voltage to {}V (ring ratio: {:.3f}, deviation: {:.3f})",
                                newVoltage, medianRingRatio, deviation);
                    if (statusCallback_) {
                        statusCallback_("Voltage: " + std::to_string(newVoltage) + "V (ring ratio: " + 
                                      std::to_string(medianRingRatio) + ")");
                    }
                }
            }
        }

        // Sleep briefly to avoid busy waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    SPDLOG_INFO("AutofocusService: Control loop stopped");
}

} // namespace backend::services


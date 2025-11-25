#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace backend::services {

class AutofocusService {
public:
    AutofocusService();
    ~AutofocusService();

    // Connection management
    bool connect(int comPort, int baudRate, unsigned char deviceAddress);
    void disconnect();
    bool isConnected() const { return connected_.load(); }

    // Autofocus control
    void setEnabled(bool enabled);
    bool isEnabled() const { return enabled_.load(); }

    // Manual voltage control
    void increaseVoltage();
    void decreaseVoltage();
    double getCurrentVoltage() const { return currentVoltage_.load(); }

    // Configuration
    struct Config {
        double focusSetpoint{20.0};
        double focusRange{0.5};
        double voltageStep{1.0};
        double fineVoltageStep{0.2};
        double maxVoltage{100.0};
        double minVoltage{0.0};
        double initialVoltage{50.0};
        double manualVoltageStep{1.0};
        int ringRatioStaleMs{1500};
        bool requireNewSamplePerStep{true};
        int minSamplesPerStep{100};
        double safeShutdownVoltage{0.0};
        bool focusDirection{true}; // true = increase voltage increases ring ratio
    };

    void setConfig(const Config& config);
    Config getConfig() const;

    // Ring ratio feed (called by ProcessingService)
    void onRingRatio(double ringRatio, int64_t timestampNs);

    // Expose running average of ring ratio for UI/status
    double getAverageRingRatio() const { return averageRingRatio_.load(std::memory_order_relaxed); }

    // Status callbacks for UI
    using StatusCallback = std::function<void(const std::string& message)>;
    void setStatusCallback(StatusCallback callback);

private:
    void controlLoop();
    void updateStatistics();
    double calculateMedian(const std::vector<double>& sorted) const;

    std::thread controlThread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::atomic<bool> enabled_{false};
    std::atomic<double> currentVoltage_{0.0};

    // Connection parameters
    int comPort_{6};
    int baudRate_{115200};
    unsigned char deviceAddress_{1};

    // Configuration
    mutable std::mutex configMutex_;
    Config config_;

    // Ring ratio buffer
    mutable std::mutex ringRatioMutex_;
    std::deque<double> ringRatioBuffer_;
    std::deque<int64_t> ringRatioTimestamps_;
    static constexpr size_t MAX_BUFFER_SIZE = 1000;
    std::atomic<uint64_t> ringRatioSequence_{0};
    std::atomic<int64_t> lastRingRatioTimestampNs_{0};

    // Statistics
    std::atomic<double> medianRingRatio_{0.0};
    std::atomic<double> averageRingRatio_{0.0};
    std::atomic<double> minRingRatio_{0.0};
    std::atomic<double> maxRingRatio_{0.0};

    // Manual control requests
    std::atomic<bool> increaseVoltageRequest_{false};
    std::atomic<bool> decreaseVoltageRequest_{false};
    std::mutex controlMutex_;

    // Status callback
    mutable std::mutex callbackMutex_;
    StatusCallback statusCallback_;

    // Last applied sequence for freshness tracking
    uint64_t lastAppliedSequence_{0};
};

} // namespace backend::services


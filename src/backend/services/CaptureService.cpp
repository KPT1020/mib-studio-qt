#include "backend/services/CaptureService.h"
#include "backend/Tools.h"
#include "backend/playback/FrameStore.h"
#include "camera/common/EGrabberCamera.h"
#include "camera/common/ICamera.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <thread>

namespace backend::services {

CaptureService::CaptureService() {
    cameraFactory_ = []() {
        return std::make_unique<camera::common::EGrabberCamera>();
    };
}

CaptureService::~CaptureService() { stop(); }

void CaptureService::setConfig(const Config& cfg) { config_ = cfg; }

void CaptureService::setFrameCallback(FrameCallback cb) { callback_ = std::move(cb); }

void CaptureService::setFrameStore(std::shared_ptr<backend::playback::FrameStore> store) { frameStore_ = std::move(store); }

void CaptureService::setCameraFactory(CameraFactory factory) {
    cameraFactory_ = std::move(factory);
}

void CaptureService::setCameraReadyCallback(CameraReadyCallback cb) {
    cameraReadyCallback_ = std::move(cb);
}

bool CaptureService::start() {
    if (running_.load()) return true;
    running_.store(true);
    thread_ = std::thread(&CaptureService::run, this);
    return true;
}

void CaptureService::stop() {
    if (!running_.load()) return;
    running_.store(false);
    {
        std::scoped_lock lk(cameraMutex_);
        if (activeCamera_) {
            activeCamera_->stop();
        }
    }
    if (thread_.joinable()) thread_.join();
}

bool CaptureService::isRunning() const { return running_.load(); }

void CaptureService::run() {
    std::unique_ptr<camera::common::ICamera> camera;

    auto releaseCamera = [&]() {
        if (cameraReadyCallback_) {
            cameraReadyCallback_(nullptr);
        }
        if (camera) {
            camera->stop();
        }
        {
            std::scoped_lock lk(cameraMutex_);
            activeCamera_ = nullptr;
        }
        camera.reset();
    };

    try {
        SPDLOG_INFO("CaptureService starting: parts={}, buffers={}", config_.bufferPartCount, config_.numBuffers);

        if (!cameraFactory_) {
            throw std::runtime_error("CaptureService has no camera factory configured");
        }

        camera = cameraFactory_();
        if (!camera) {
            throw std::runtime_error("CaptureService camera factory returned null");
        }

        camera::common::CameraConfig camCfg;
        camCfg.bufferPartCount = config_.bufferPartCount;
        camCfg.numBuffers = config_.numBuffers;
        camera->applyConfig(camCfg);

        {
            std::scoped_lock lk(cameraMutex_);
            activeCamera_ = camera.get();
        }

        if (!camera->start()) {
            throw std::runtime_error("CaptureService camera failed to start");
        }

        if (cameraReadyCallback_) {
            cameraReadyCallback_(camera.get());
        }

        constexpr uint64_t kStatsInterval = 1'000'000ULL;
        constexpr uint64_t kHealthCheckInterval = 5'000'000ULL;  // 5 seconds
        uint64_t nextStatsPoll = Tools::getTimestamp() + kStatsInterval;
        uint64_t nextHealthCheck = Tools::getTimestamp() + kHealthCheckInterval;

        // Clock-offset tracker (cpu_steady_ns - hw_frame_timestamp_ns). The camera
        // hardware timestamps arrive on a periodic, jitter-free grid; the CPU
        // steady_clock reading taken after grabFrame() returns is subject to
        // scheduling jitter driven by processing-pipeline load. We track a slow
        // EMA of the offset so we can predict the "ideal" CPU-clock capture time
        // for each frame (hw_ns + offsetSmoothed). That prediction is what we
        // push into FrameStore's sideband and what TriggerService uses as the
        // scheduling anchor, so pulses land on the hardware periodic grid
        // regardless of CPU-side jitter.
        int64_t clockOffsetSmoothedNs = 0;
        bool clockOffsetInitialized = false;
        // EMA weight = 1/alphaDenom. 1/64 (≈0.016) is slow enough to reject CPU
        // jitter but fast enough to track gradual drift between the two clocks.
        constexpr int64_t kOffsetEmaDenom = 64;
        // If the raw offset jumps by more than this, assume the hardware clock
        // restarted (e.g. camera re-armed) and re-bootstrap the smoothed value.
        constexpr int64_t kOffsetResetThresholdNs = 100'000'000LL; // 100 ms

        while (running_.load()) {
            // Periodic health check
            const uint64_t now = Tools::getTimestamp();
            if (now >= nextHealthCheck) {
                if (!camera->checkDeviceHealth()) {
                    SPDLOG_WARN("CaptureService: Device health check failed, stopping capture gracefully");
                    break;
                }
                nextHealthCheck = now + kHealthCheckInterval;
            }

            camera::common::Frame frame;
            if (!camera->grabFrame(frame)) {
                if (!running_.load()) {
                    break;
                }
                if (!camera->isRunning()) {
                    SPDLOG_INFO("CaptureService camera stopped streaming");
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }

            // Record steady_clock timestamp as close to frame capture as possible.
            // This gives us the CPU-side observation of when the frame arrived;
            // it is subject to scheduling jitter driven by processing-pipeline load.
            const int64_t cpuObservedNs = static_cast<int64_t>(
                std::chrono::steady_clock::now().time_since_epoch().count());

            // Derive a jitter-free "predicted" CPU capture time by combining the
            // hardware timestamp (periodic, jitter-free) with a slow EMA of the
            // hw-to-CPU offset. This is what the downstream trigger pipeline uses
            // as its scheduling anchor, so onsets land on the hardware grid even
            // when CPU-side capture timing drifts under load.
            uint64_t captureSteadyNs = static_cast<uint64_t>(cpuObservedNs);
            if (frame.timestamp != 0) {
                const int64_t hwNs = static_cast<int64_t>(frame.timestamp);
                const int64_t rawOffset = cpuObservedNs - hwNs;
                stats_.lastRawOffsetNs.store(rawOffset, std::memory_order_relaxed);

                const bool reset = !clockOffsetInitialized
                    || std::llabs(rawOffset - clockOffsetSmoothedNs) > kOffsetResetThresholdNs;
                if (reset) {
                    if (clockOffsetInitialized) {
                        SPDLOG_WARN("CaptureService: hw-clock offset jumped by {} ns; re-bootstrapping",
                                    rawOffset - clockOffsetSmoothedNs);
                    }
                    clockOffsetSmoothedNs = rawOffset;
                    clockOffsetInitialized = true;
                } else {
                    // EMA: smoothed += (raw - smoothed) / denom
                    clockOffsetSmoothedNs +=
                        (rawOffset - clockOffsetSmoothedNs) / kOffsetEmaDenom;
                }
                stats_.clockOffsetNs.store(clockOffsetSmoothedNs, std::memory_order_relaxed);

                // Predicted CPU-clock capture instant on the hardware periodic grid.
                captureSteadyNs = static_cast<uint64_t>(hwNs + clockOffsetSmoothedNs);
            }

            if (callback_) {
                callback_(frame.data.data(),
                          frame.data.size(),
                          frame.width,
                          frame.height,
                          frame.timestamp);
            }
            if (frameStore_) {
                frameStore_->pushFrame(frame.data.data(),
                                       frame.data.size(),
                                       frame.width,
                                       frame.height,
                                       frame.linePitch,
                                       frame.pixelFormat,
                                       frame.timestamp,
                                       captureSteadyNs);
            }
            stats_.framesProcessed.fetch_add(1, std::memory_order_relaxed);

            if (now >= nextStatsPoll) {
                camera::common::CameraStats cameraStats{};
                if (camera->pollStats(cameraStats)) {
                    stats_.lastFrameRate.store(cameraStats.frameRate, std::memory_order_relaxed);
                    stats_.lastDataRateMBps.store(cameraStats.dataRateMBps, std::memory_order_relaxed);
                    SPDLOG_DEBUG("Capture stats: {} fps, {} MB/s", cameraStats.frameRate, cameraStats.dataRateMBps);
                }
                nextStatsPoll = now + kStatsInterval;
            }
        }

        if (camera) {
            camera::common::CameraStats cameraStats{};
            if (camera->pollStats(cameraStats)) {
                stats_.lastFrameRate.store(cameraStats.frameRate, std::memory_order_relaxed);
                stats_.lastDataRateMBps.store(cameraStats.dataRateMBps, std::memory_order_relaxed);
            }
        }

        releaseCamera();
        running_.store(false);
        SPDLOG_INFO("CaptureService stopped");
    } catch (const std::exception& ex) {
        SPDLOG_ERROR("CaptureService exception: {}", ex.what());
        releaseCamera();
        running_.store(false);
    } catch (...) {
        SPDLOG_ERROR("CaptureService unknown exception");
        releaseCamera();
        running_.store(false);
    }
}

} // namespace backend::services

#include "backend/services/CaptureService.h"
#include "backend/app/Tools.h"
#include "backend/diagnostics/CrashStateMirror.h"
#include "backend/playback/FrameStore.h"
#include "backend/camera/egrabber/EGrabberCamera.h"
#include "backend/camera/common/ICamera.h"
#include "backend/camera/mock/MockCamera.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <thread>

namespace backend::services {

CaptureService::CaptureService() {
#if MIB_HAS_EGRABBER
    cameraFactory_ = []() {
        return std::make_unique<camera::common::EGrabberCamera>();
    };
#else
    cameraFactory_ = []() {
        camera::mock::MockCameraOptions options;
        options.folder = std::filesystem::path("data") / "mock_frames";
        return std::make_unique<camera::mock::MockCamera>(options);
    };
#endif
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
    const bool wasRunning = running_.exchange(false);
    if (wasRunning) {
        // Stop the trigger thread BEFORE tearing the camera down: it calls
        // ICamera::setTriggerOutput and must not race the grabber teardown.
        // releaseCamera() on the capture thread repeats this call later; both
        // the callback and TriggerService::stop() are idempotent.
        if (cameraReadyCallback_) {
            cameraReadyCallback_(nullptr);
        }
        std::scoped_lock lk(cameraMutex_);
        if (activeCamera_) {
            activeCamera_->stop();
        }
    }
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) {
        thread_.join();
    }
}

bool CaptureService::isRunning() const { return running_.load(); }

bool CaptureService::softTriggerActiveCamera() {
    std::scoped_lock lk(cameraMutex_);
    return activeCamera_ && activeCamera_->softTrigger();
}

void CaptureService::run() {
    std::unique_ptr<camera::common::ICamera> camera;

    auto releaseCamera = [&]() {
        // Without a running camera there is no confirmed mode; consumers
        // (status-bar badge) fall back to the requested config mode, so a mode
        // changed between runs shows up immediately as "(requested)".
        stats_.deliveryModeConfirmed.store(false, std::memory_order_release);
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
        SPDLOG_INFO("CaptureService starting: parts={}, buffers={}, mode={}",
                    config_.bufferPartCount, config_.numBuffers,
                    camera::common::toString(config_.deliveryMode));
        stats_.requestedDeliveryMode.store(static_cast<int>(config_.deliveryMode),
                                           std::memory_order_relaxed);
        stats_.deliveryModeConfirmed.store(false, std::memory_order_release);
        {
            auto& m = backend::diagnostics::CrashStateMirror::instance().capture;
            m.running.store(true, std::memory_order_relaxed);
            m.bufferPartCount.store(config_.bufferPartCount, std::memory_order_relaxed);
            m.numBuffers.store(config_.numBuffers, std::memory_order_relaxed);
        }

        if (!cameraFactory_) {
            throw std::runtime_error("CaptureService has no camera factory configured");
        }

        camera = cameraFactory_();
        if (!camera) {
            throw std::runtime_error("CaptureService camera factory returned null");
        }

        const auto deliveryCaps = camera->deliveryCapabilities();
        if (config_.deliveryMode == camera::common::FrameDeliveryMode::LatestFrame &&
            !deliveryCaps.supportsLatestFrame) {
            throw std::runtime_error(
                "This camera backend does not support Latest Frame delivery; "
                "select Every Frame or use a backend with newest-frame support");
        }
        if (config_.deliveryMode == camera::common::FrameDeliveryMode::EveryFrame &&
            !deliveryCaps.supportsEveryFrame) {
            throw std::runtime_error(
                "This camera backend does not support Every Frame delivery; "
                "select Latest Frame");
        }

        camera::common::CameraConfig camCfg;
        camCfg.bufferPartCount = config_.bufferPartCount;
        camCfg.numBuffers = config_.numBuffers;
        camCfg.deliveryMode = config_.deliveryMode;
        camera->applyConfig(camCfg);

        {
            std::scoped_lock lk(cameraMutex_);
            activeCamera_ = camera.get();
        }

        if (!camera->start()) {
            throw std::runtime_error("CaptureService camera failed to start");
        }

        stats_.activeDeliveryMode.store(static_cast<int>(camera->activeDeliveryMode()),
                                        std::memory_order_relaxed);
        stats_.deliveryModeConfirmed.store(true, std::memory_order_release);

        if (cameraReadyCallback_) {
            cameraReadyCallback_(camera.get());
        }

        constexpr uint64_t kStatsInterval = 1'000'000ULL;
        constexpr uint64_t kHealthCheckInterval = 5'000'000ULL;  // 5 seconds
        uint64_t nextStatsPoll = Tools::getTimestamp() + kStatsInterval;
        uint64_t nextHealthCheck = Tools::getTimestamp() + kHealthCheckInterval;

        // Largest frame size reserved in the FIFO so far this session. Drives a
        // one-time (per geometry) pre-reservation so the high-speed push hot path
        // never allocates.
        size_t reservedFrameBytes = 0;

        // Declared outside the loop so grabFrame's data.assign reuses the
        // vector's capacity frame-to-frame (pushFrame copies out of it) —
        // at triggered rates of 5000 fps a per-iteration Frame would malloc
        // the pixel buffer every 200 µs.
        camera::common::Frame frame;

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

            const bool grabbed = camera->grabFrame(frame);
            // Host monotonic acquisition stamp: the anchor every downstream
            // latency measurement (algo start/end, trigger fire) compares
            // against. The frame's own `timestamp` is a device tick and lives
            // on a different clock — unless the backend has verified the
            // domains match (timestampsHostComparable), in which case the
            // difference is the frame's age in the SDK queues.
            frame.hostTimestampUs = Tools::getTimestamp();
            if (grabbed && deliveryCaps.timestampsHostComparable &&
                frame.timestamp <= frame.hostTimestampUs) {
                stats_.lastFrameAgeUs.store(frame.hostTimestampUs - frame.timestamp,
                                            std::memory_order_relaxed);
                stats_.frameAgeValid.store(true, std::memory_order_relaxed);
            }
            if (!grabbed) {
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

            if (callback_) {
                callback_(frame.data.data(),
                          frame.data.size(),
                          frame.width,
                          frame.height,
                          frame.timestamp);
            }
            if (frameStore_) {
                // Pre-reserve FIFO slots to the live frame size so the
                // high-speed hot path never allocates. First reservation happens
                // on frame 1; re-reserve only if the geometry grows.
                const size_t frameBytes = frame.data.size();
                if (frameBytes > reservedFrameBytes) {
                    frameStore_->reserveFrameBytes(frameBytes);
                    reservedFrameBytes = frameBytes;
                }
                frameStore_->pushFrame(frame.data.data(),
                                       frame.data.size(),
                                       frame.width,
                                       frame.height,
                                       frame.linePitch,
                                       frame.pixelFormat,
                                       frame.timestamp,
                                       frame.hostTimestampUs);
            }
            stats_.lastPublishLatencyUs.store(Tools::getTimestamp() - frame.hostTimestampUs,
                                              std::memory_order_relaxed);
            stats_.framesProcessed.fetch_add(1, std::memory_order_relaxed);
            backend::diagnostics::CrashStateMirror::instance().capture.framesProcessed
                .fetch_add(1, std::memory_order_relaxed);

            if (now >= nextStatsPoll) {
                camera::common::CameraStats cameraStats{};
                if (camera->pollStats(cameraStats)) {
                    stats_.lastFrameRate.store(cameraStats.frameRate, std::memory_order_relaxed);
                    stats_.lastDataRateMBps.store(cameraStats.dataRateMBps, std::memory_order_relaxed);
                    auto& m = backend::diagnostics::CrashStateMirror::instance().capture;
                    m.lastFrameRate.store(cameraStats.frameRate, std::memory_order_relaxed);
                    m.lastDataRateMBps.store(cameraStats.dataRateMBps, std::memory_order_relaxed);
                    SPDLOG_DEBUG("Capture stats: {} fps, {} MB/s", cameraStats.frameRate, cameraStats.dataRateMBps);
                }
                camera::common::AcquisitionQueueStats queueStats{};
                if (camera->pollAcquisitionQueueStats(queueStats)) {
                    stats_.intentionallyDiscardedFrames.store(
                        queueStats.intentionallyDiscardedFrames, std::memory_order_relaxed);
                    stats_.transportLostFrames.store(queueStats.transportLostFrames,
                                                     std::memory_order_relaxed);
                    stats_.bufferUnderruns.store(queueStats.bufferUnderruns,
                                                 std::memory_order_relaxed);
                    stats_.sdkCompletedQueueDepth.store(queueStats.sdkCompletedQueueDepth,
                                                        std::memory_order_relaxed);
                    stats_.sdkInputBufferCount.store(queueStats.sdkInputBufferCount,
                                                     std::memory_order_relaxed);
                    stats_.queueStatsValid.store(true, std::memory_order_release);
                    // In EveryFrame mode a growing completed-buffer backlog is
                    // hidden latency; surface it (rate-limited to this 1 s
                    // stats interval).
                    if (config_.deliveryMode == camera::common::FrameDeliveryMode::EveryFrame &&
                        queueStats.completedQueueDepthValid &&
                        queueStats.sdkCompletedQueueDepth > 0) {
                        SPDLOG_WARN("CaptureService: {} completed SDK buffers backlogged in "
                                    "EveryFrame mode (latency is growing)",
                                    queueStats.sdkCompletedQueueDepth);
                    }
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
        backend::diagnostics::CrashStateMirror::instance().capture.running.store(false);
        SPDLOG_INFO("CaptureService stopped");
    } catch (const std::exception& ex) {
        SPDLOG_ERROR("CaptureService exception: {}", ex.what());
        releaseCamera();
        running_.store(false);
        backend::diagnostics::CrashStateMirror::instance().capture.running.store(false);
    } catch (...) {
        SPDLOG_ERROR("CaptureService unknown exception");
        releaseCamera();
        running_.store(false);
        backend::diagnostics::CrashStateMirror::instance().capture.running.store(false);
    }
}

} // namespace backend::services

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

        // Largest frame size reserved in the FIFO so far this session. Drives a
        // one-time (per geometry) pre-reservation so the high-speed push hot path
        // never allocates.
        size_t reservedFrameBytes = 0;

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
                                       frame.timestamp);
            }
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

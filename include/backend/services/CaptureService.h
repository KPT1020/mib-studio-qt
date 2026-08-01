#pragma once

#include "backend/camera/common/ICamera.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace backend { namespace playback { class FrameStore; } }

namespace backend::services {

struct CaptureStats {
    std::atomic<uint64_t> framesProcessed{0};
    std::atomic<uint64_t> lastFrameRate{0};     // from StreamModule StatisticsFrameRate
    std::atomic<uint64_t> lastDataRateMBps{0};  // from StreamModule StatisticsDataRate

    // Delivery mode + acquisition-queue telemetry. Modes are stored as the
    // integer value of camera::common::FrameDeliveryMode so the struct stays
    // lock-free; deliveryModeConfirmed distinguishes the backend-confirmed
    // active mode from the merely requested one.
    std::atomic<int> requestedDeliveryMode{0};
    std::atomic<int> activeDeliveryMode{0};
    std::atomic<bool> deliveryModeConfirmed{false};
    std::atomic<uint64_t> intentionallyDiscardedFrames{0};
    std::atomic<uint64_t> transportLostFrames{0};
    std::atomic<uint64_t> bufferUnderruns{0};
    std::atomic<uint64_t> sdkCompletedQueueDepth{0};
    std::atomic<uint64_t> sdkInputBufferCount{0};
    std::atomic<bool> queueStatsValid{false};
    // Age of the last grabbed frame (device stamp -> host dequeue), only
    // populated when the backend reports timestampsHostComparable.
    std::atomic<uint64_t> lastFrameAgeUs{0};
    std::atomic<bool> frameAgeValid{false};
    // Host dequeue -> post-publish (callback + FrameStore copy) duration.
    std::atomic<uint64_t> lastPublishLatencyUs{0};
};

class CaptureService {
public:
    using FrameCallback = std::function<void(const uint8_t* data,
                                             size_t size,
                                             uint64_t width,
                                             uint64_t height,
                                             uint64_t timestampNs)>;

    using CameraFactory = std::function<std::unique_ptr<camera::common::ICamera>()>;

    struct Config {
        int bufferPartCount = 1;  // number of images per buffer
        int numBuffers = 20;        // ring size
        // Requested SDK-queue policy; backends confirm or reject at start().
        camera::common::FrameDeliveryMode deliveryMode =
            camera::common::FrameDeliveryMode::EveryFrame;
    };

    CaptureService();
    ~CaptureService();

    void setConfig(const Config& cfg);
    void setFrameCallback(FrameCallback cb);

    // Optional: store frames to a shared ring for playback/display
    void setFrameStore(std::shared_ptr<backend::playback::FrameStore> store);

    void setCameraFactory(CameraFactory factory);

    // Callback fired when camera starts (with pointer) or stops (with nullptr)
    using CameraReadyCallback = std::function<void(camera::common::ICamera*)>;
    void setCameraReadyCallback(CameraReadyCallback cb);

    bool start();
    void stop();
    bool isRunning() const;

    const CaptureStats& stats() const { return stats_; }

    // Backend-confirmed active mode (falls back to the requested mode until a
    // camera has confirmed one). Prefer this over Config::deliveryMode when
    // displaying state to the user.
    camera::common::FrameDeliveryMode activeDeliveryMode() const {
        return stats_.deliveryModeConfirmed.load(std::memory_order_acquire)
                   ? static_cast<camera::common::FrameDeliveryMode>(
                         stats_.activeDeliveryMode.load(std::memory_order_acquire))
                   : config_.deliveryMode;
    }

private:
    void run();

    Config config_{};
    FrameCallback callback_{};
    std::shared_ptr<backend::playback::FrameStore> frameStore_{};

    CameraFactory cameraFactory_{};
    CameraReadyCallback cameraReadyCallback_;
    camera::common::ICamera* activeCamera_{nullptr};
    std::mutex cameraMutex_;

    std::thread thread_;
    std::atomic<bool> running_{false};

    CaptureStats stats_{};
};

} // namespace backend::services

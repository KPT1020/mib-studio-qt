#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace camera::common {
class ICamera;
struct CameraConfig;
}

namespace backend { namespace playback { class FrameStore; } }

namespace backend::services {

struct CaptureStats {
    std::atomic<uint64_t> framesProcessed{0};
    std::atomic<uint64_t> lastFrameRate{0};     // from StreamModule StatisticsFrameRate
    std::atomic<uint64_t> lastDataRateMBps{0};  // from StreamModule StatisticsDataRate
    // EMA-smoothed offset between CPU steady_clock and the camera's hardware
    // timestamp clock, in nanoseconds (cpu_ns - hw_ns). Used to derive a
    // jitter-free "predicted" CPU capture time = frame.timestamp + offset, so
    // downstream trigger scheduling lands on the hardware's periodic grid
    // regardless of CPU-side scheduling jitter caused by processing load.
    // 0 means "not initialized" or "no hardware timestamp available".
    std::atomic<int64_t> clockOffsetNs{0};
    // Raw latest observed offset (not smoothed); useful for debugging.
    std::atomic<int64_t> lastRawOffsetNs{0};
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

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace backend::services {

struct CaptureStats {
    std::atomic<uint64_t> framesProcessed{0};
    std::atomic<uint64_t> lastFrameRate{0};     // from StreamModule StatisticsFrameRate
    std::atomic<uint64_t> lastDataRateMBps{0};  // from StreamModule StatisticsDataRate
};

class CaptureService {
public:
    using FrameCallback = std::function<void(const uint8_t* data,
                                             size_t size,
                                             uint64_t width,
                                             uint64_t height,
                                             uint64_t timestampNs)>;

    struct Config {
        int bufferPartCount = 100;  // number of images per buffer
        int numBuffers = 20;        // ring size
    };

    CaptureService();
    ~CaptureService();

    void setConfig(const Config& cfg);
    void setFrameCallback(FrameCallback cb);

    bool start();
    void stop();
    bool isRunning() const;

    const CaptureStats& stats() const { return stats_; }

private:
    void run();

    Config config_{};
    FrameCallback callback_{};

    std::thread thread_;
    std::atomic<bool> running_{false};

    CaptureStats stats_{};
};

} // namespace backend::services

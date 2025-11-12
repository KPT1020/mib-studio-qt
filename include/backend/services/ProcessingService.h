#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <condition_variable>
#include <opencv2/core.hpp>

namespace backend { namespace playback { class FrameStore; } }

namespace backend::services {

struct ProcessingStats {
    std::atomic<uint64_t> jobsQueued{0};
    std::atomic<uint64_t> jobsProcessed{0};
};

class ProcessingService {
public:
    using Job = std::function<void()>;

    struct Roi {
        int x{0}, y{0}, w{0}, h{0};
    };

    struct RealtimeSnapshot {
        uint64_t index{0};
        std::vector<std::vector<cv::Point>> contours;
        cv::Mat mask;
    };

    ProcessingService();
    ~ProcessingService();

    void start(size_t workerCount = std::thread::hardware_concurrency());
    void stop();

    void submit(Job job);

    const ProcessingStats& stats() const { return stats_; }

    // Realtime processing API
    void startRealtime(std::shared_ptr<backend::playback::FrameStore> store);
    void stopRealtime();
    void setRealtimeEnabled(bool on);
    void setRealtimeRoi(const Roi& roi);
    void setRealtimeBackgroundGray(const cv::Mat& bg);
    bool getLatestSnapshot(RealtimeSnapshot& out);

private:
    void workerLoop();
    void realtimeLoop();

    std::vector<std::thread> workers_;
    std::queue<Job> queue_;
    std::mutex mutex_;
    std::condition_variable_any cv_;
    std::atomic<bool> running_{false};

    ProcessingStats stats_{};

    // Realtime processing state
    std::thread realtimeThread_;
    std::atomic<bool> rtRunning_{false};
    std::atomic<bool> rtEnabled_{true};
    std::shared_ptr<backend::playback::FrameStore> rtStore_;
    std::mutex rtMutex_;
    Roi rtRoi_{};
    cv::Mat rtBgGray_; // protected by rtMutex_
    std::atomic<uint64_t> rtLastProcessed_{0};

    std::mutex snapshotMutex_;
    RealtimeSnapshot latestSnapshot_;
};

} // namespace backend::services

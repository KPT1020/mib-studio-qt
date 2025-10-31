#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <condition_variable>

namespace backend::services {

struct ProcessingStats {
    std::atomic<uint64_t> jobsQueued{0};
    std::atomic<uint64_t> jobsProcessed{0};
};

class ProcessingService {
public:
    using Job = std::function<void()>;

    ProcessingService();
    ~ProcessingService();

    void start(size_t workerCount = std::thread::hardware_concurrency());
    void stop();

    void submit(Job job);

    const ProcessingStats& stats() const { return stats_; }

private:
    void workerLoop();

    std::vector<std::thread> workers_;
    std::queue<Job> queue_;
    std::mutex mutex_;
    std::condition_variable_any cv_;
    std::atomic<bool> running_{false};

    ProcessingStats stats_{};
};

} // namespace backend::services

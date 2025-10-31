#include "backend/services/ProcessingService.h"

#include <spdlog/spdlog.h>

namespace backend::services {

ProcessingService::ProcessingService() = default;
ProcessingService::~ProcessingService() { stop(); }

void ProcessingService::start(size_t workerCount) {
    if (running_.load()) return;
    running_.store(true);
    if (workerCount == 0) workerCount = 1;
    workers_.reserve(workerCount);
    for (size_t i = 0; i < workerCount; ++i) {
        workers_.emplace_back(&ProcessingService::workerLoop, this);
    }
    SPDLOG_INFO("ProcessingService started with {} workers", workerCount);
}

void ProcessingService::stop() {
    if (!running_.load()) return;
    running_.store(false);
    cv_.notify_all();
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
    workers_.clear();
    // drain queue
    {
        std::scoped_lock lk(mutex_);
        std::queue<Job> empty;
        queue_.swap(empty);
    }
    SPDLOG_INFO("ProcessingService stopped");
}

void ProcessingService::submit(Job job) {
    if (!running_.load()) return;
    {
        std::scoped_lock lk(mutex_);
        queue_.push(std::move(job));
        stats_.jobsQueued.fetch_add(1, std::memory_order_relaxed);
    }
    cv_.notify_one();
}

void ProcessingService::workerLoop() {
    while (running_.load()) {
        Job job;
        {
            std::unique_lock lk(mutex_);
            cv_.wait(lk, [&]{ return !running_.load() || !queue_.empty(); });
            if (!running_.load()) break;
            if (queue_.empty()) continue;
            job = std::move(queue_.front());
            queue_.pop();
        }
        if (job) {
            job();
            stats_.jobsProcessed.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

} // namespace backend::services

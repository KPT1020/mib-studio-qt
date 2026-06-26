// Hang watchdog for tests that spawn threads / drive the pipeline.
// If no mark() lands within `stuckSeconds`, prints the last step and _Exit(99)s,
// so a deadlock fails fast with a diagnostic instead of burning the ctest
// timeout. Use _Exit, NOT abort(): the linked crash handler intercepts abort().
#pragma once

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace mib::test {

class Watchdog {
public:
    explicit Watchdog(int stuckSeconds = 20) : stuckSeconds_(stuckSeconds)
    {
        lastMs_.store(nowMs());
        running_.store(true);
        thread_ = std::thread([this] { loop(); });
    }

    ~Watchdog()
    {
        running_.store(false);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    // Record progress; `step` must be a string literal / static-lifetime string.
    void mark(const char* step)
    {
        step_.store(step, std::memory_order_relaxed);
        lastMs_.store(nowMs(), std::memory_order_relaxed);
    }

private:
    static long long nowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    void loop()
    {
        while (running_.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            if (nowMs() - lastMs_.load(std::memory_order_relaxed) >
                stuckSeconds_ * 1000LL) {
                std::fprintf(stderr, "\nWATCHDOG: stuck >%ds at step=%s\n",
                             stuckSeconds_, step_.load(std::memory_order_relaxed));
                std::fflush(stderr);
                std::_Exit(99);
            }
        }
    }

    int stuckSeconds_;
    std::atomic<bool> running_{false};
    std::atomic<const char*> step_{"start"};
    std::atomic<long long> lastMs_{0};
    std::thread thread_;
};

} // namespace mib::test

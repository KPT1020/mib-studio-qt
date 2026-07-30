#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace backend::diagnostics {

// Name -> OS thread id map for the pipeline's long-running threads, so a
// low-rate profiler (PipelineTrendSampler) can attribute CPU time and
// context switches per pipeline stage instead of per process. Threads call
// registerCurrentThread("<stage>") once at loop entry; registration for an
// already-registered tid just refreshes the name. Entries are never removed
// on thread exit — the sampler tolerates dead tids (their counters simply
// stop moving) and pipeline threads are recreated with the same names.
class ThreadRegistry {
public:
    struct Entry {
        std::string name;
        uint64_t tid{0}; // gettid() on Linux, GetCurrentThreadId() on Windows
    };

    static ThreadRegistry& instance();

    // Cheap (one mutex acquisition), called once per thread start.
    void registerCurrentThread(const char* name);

    std::vector<Entry> snapshot() const;

private:
    ThreadRegistry() = default;
    mutable std::mutex mutex_;
    std::vector<Entry> entries_;
};

} // namespace backend::diagnostics

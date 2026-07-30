#include "backend/diagnostics/ThreadRegistry.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace backend::diagnostics {

namespace {

uint64_t currentThreadId() {
#ifdef _WIN32
    return static_cast<uint64_t>(GetCurrentThreadId());
#else
    return static_cast<uint64_t>(::syscall(SYS_gettid));
#endif
}

} // namespace

ThreadRegistry& ThreadRegistry::instance() {
    static ThreadRegistry registry;
    return registry;
}

void ThreadRegistry::registerCurrentThread(const char* name) {
    const uint64_t tid = currentThreadId();
    std::scoped_lock lk(mutex_);
    for (auto& e : entries_) {
        if (e.tid == tid) {
            e.name = name;
            return;
        }
    }
    entries_.push_back({name, tid});
}

std::vector<ThreadRegistry::Entry> ThreadRegistry::snapshot() const {
    std::scoped_lock lk(mutex_);
    return entries_;
}

} // namespace backend::diagnostics

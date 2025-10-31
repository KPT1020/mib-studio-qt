#include "backend/Tools.h"

#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

namespace backend {

uint64_t Tools::getTimestamp() {
#ifdef _WIN32
    LARGE_INTEGER freqc, nowc;
    uint64_t freq, now;

    QueryPerformanceFrequency(&freqc);
    QueryPerformanceCounter(&nowc);
    freq = static_cast<uint64_t>(freqc.QuadPart);
    now = static_cast<uint64_t>(nowc.QuadPart);

    return (now * 1000000ULL) / freq;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (static_cast<uint64_t>(ts.tv_sec) * 1000000ULL) + (ts.tv_nsec / 1000ULL);
#endif
}

void Tools::log(const std::string& msg) {
    SPDLOG_INFO("{}", msg);
}

} // namespace backend

#include "backend/Tools.h"

#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "Psapi.lib")
#pragma comment(lib, "Kernel32.lib")
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

#ifdef _WIN32
static inline double bytesToMB(SIZE_T bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}
#endif

double Tools::getProcessMemoryMB() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&pmc),
                             sizeof(pmc))) {
        // Prefer PrivateUsage when available, fallback to WorkingSetSize
        const SIZE_T bytes = pmc.PrivateUsage ? pmc.PrivateUsage : pmc.WorkingSetSize;
        return bytesToMB(bytes);
    }
    return 0.0;
#else
    return 0.0;
#endif
}

double Tools::getPeakProcessMemoryMB() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        // PeakWorkingSetSize is commonly used; PeakPagefileUsage could also be considered
        return bytesToMB(pmc.PeakWorkingSetSize);
    }
    return 0.0;
#else
    return 0.0;
#endif
}

uint64_t Tools::getAvailableSystemRAMBytes() {
#ifdef _WIN32
    MEMORYSTATUSEX memStatus{};
    memStatus.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memStatus)) {
        return static_cast<uint64_t>(memStatus.ullAvailPhys);
    }
    return 0;
#else
    return 0;
#endif
}

} // namespace backend

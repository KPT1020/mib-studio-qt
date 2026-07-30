#pragma once

#include <string>
#include <chrono>
#include <cstdint>
#include <vector>

namespace backend {

class Tools {
public:
    // High-precision timestamp in microseconds (matches SDK pattern)
    static uint64_t getTimestamp();

    // Enumerate available COM port numbers (e.g. 1 for COM1, 3 for COM3).
    // Windows: registry HKLM\HARDWARE\DEVICEMAP\SERIALCOMM; other platforms: empty.
    static std::vector<int> availableComPortNumbers();

    // Process memory usage (MB). Windows PrivateUsage / Linux VmRSS; returns
    // 0 on unsupported platforms.
    static double getProcessMemoryMB();

    // Peak process memory usage (MB). Windows PeakWorkingSetSize / Linux
    // VmHWM; returns 0 on unsupported platforms.
    static double getPeakProcessMemoryMB();

    // Available system RAM in bytes. Windows-first; returns 0 on unsupported platforms.
    static uint64_t getAvailableSystemRAMBytes();

    // Allocator-level heap stats (glibc mallinfo2; zeros elsewhere). The gap
    // between process RSS and inUseMB is the fragmentation/arena signal a
    // plain RSS reading cannot show.
    struct HeapStats {
        double inUseMB{0.0}; // bytes handed out to the application
        double freeMB{0.0};  // bytes held by the allocator, not returned to the OS
    };
    static HeapStats getHeapStats();

    // Cumulative bytes this process has written to storage
    // (/proc/self/io write_bytes; 0 on unsupported platforms).
    static double getProcessIoWriteMB();

    // String conversion utilities
    template <typename T>
    static std::string toString(const T& v) {
        return std::to_string(v);
    }

    // Logging utility (uses spdlog internally)
    static void log(const std::string& msg);

private:
    Tools() = delete; // static utility class
};

} // namespace backend

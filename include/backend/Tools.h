#pragma once

#include <string>
#include <chrono>
#include <cstdint>

namespace backend {

class Tools {
public:
    // High-precision timestamp in microseconds (matches SDK pattern)
    static uint64_t getTimestamp();

    // Process memory usage (MB). Windows-first; returns 0 on unsupported platforms.
    static double getProcessMemoryMB();

    // Peak process memory usage (MB). Windows-first; returns 0 on unsupported platforms.
    static double getPeakProcessMemoryMB();

    // Available system RAM in bytes. Windows-first; returns 0 on unsupported platforms.
    static uint64_t getAvailableSystemRAMBytes();

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

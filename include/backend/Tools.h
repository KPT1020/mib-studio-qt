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
    // Windows-only compatibility wrapper over availableSerialPortNames().
    static std::vector<int> availableComPortNumbers();

    // Enumerate available serial port names (e.g. COM3, ttyUSB0, cu.usbserial-*)
    // using Qt's cross-platform serial metadata.
    static std::vector<std::string> availableSerialPortNames();

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

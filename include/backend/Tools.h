#pragma once

#include <string>
#include <chrono>
#include <cstdint>

namespace backend {

class Tools {
public:
    // High-precision timestamp in microseconds (matches SDK pattern)
    static uint64_t getTimestamp();

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

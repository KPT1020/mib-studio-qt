// Helpers for hardware-present tests. These tests are labeled "hardware" and
// excluded from the default CTest presets; they run only via the hardware preset
// or `ctest -L hardware`. When the device's env var is absent they self-skip
// with exit code 77 (CTest SKIP_RETURN_CODE) so they don't fail an operator's
// normal run.
#pragma once

#include <cstdio>
#include <cstdlib>

namespace mib::test {

inline constexpr int kSkipExitCode = 77;

// Returns the env value, or self-skips (exit 77) if unset/empty.
inline const char* requireDeviceEnv(const char* name)
{
    const char* v = std::getenv(name);
    if (v == nullptr || *v == '\0') {
        std::printf("SKIP: %s not set; hardware not present for this test\n", name);
        std::fflush(stdout);
        std::_Exit(kSkipExitCode);
    }
    std::printf("hardware test using %s=%s\n", name, v);
    return v;
}

inline int envInt(const char* name, int fallback)
{
    const char* v = std::getenv(name);
    if (v == nullptr || *v == '\0') return fallback;
    return std::atoi(v);
}

} // namespace mib::test

// Minimal test assertions for the bespoke main()-based test harness.
// MIB_EXPECT records a failure and continues; MIB_REQUIRE aborts the test.
// Return mib::test::exitCode() from main().
#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>

namespace mib::test {

inline int& failureCount()
{
    static int n = 0;
    return n;
}

inline int exitCode()
{
    return failureCount() == 0 ? 0 : 1;
}

} // namespace mib::test

#define MIB_EXPECT(cond, msg)                                                  \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "EXPECT FAILED: %s (%s:%d) %s\n", #cond,      \
                         __FILE__, __LINE__, std::string(msg).c_str());        \
            std::fflush(stderr);                                               \
            ++::mib::test::failureCount();                                     \
        }                                                                      \
    } while (0)

#define MIB_REQUIRE(cond, msg)                                                 \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "REQUIRE FAILED: %s (%s:%d) %s\n", #cond,     \
                         __FILE__, __LINE__, std::string(msg).c_str());        \
            std::fflush(stderr);                                               \
            std::_Exit(1);                                                     \
        }                                                                      \
    } while (0)

// startup_probe_test
//
// Unit coverage for backend::diagnostics::StartupProbe, the startup lifeline
// behind the silent-launch diagnosis flow (docs/howto/troubleshoot-crashes.md,
// "Application will not open — no error, no window"):
//   * a fresh begin() finds no previous attempt and arms the marker file
//   * a marker left by a dead process is reported with its last stage,
//     then consumed (the next begin() does not re-report it)
//   * complete() removes the marker so a finished startup is never reported
//   * a marker whose pid is still alive (concurrent second instance) is NOT
//     reported as a failed launch
//   * stage()/complete() before begin() are safe no-ops

#include "backend/diagnostics/StartupProbe.h"

#include "support/watchdog.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <process.h>
#define MIB_TEST_GETPID _getpid
#else
#include <unistd.h>
#define MIB_TEST_GETPID getpid
#endif

using backend::diagnostics::StartupProbe;

namespace {

int failures = 0;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << " " << #cond << "\n";           \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

// A pid that is close to certain not to exist on the test machine.
constexpr long kDeadPid = 1999999999L;

std::filesystem::path makeTestDir(const char* name)
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                      ("mib_startup_probe_" + std::to_string(MIB_TEST_GETPID()) +
                                       "_" + name);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

void testFreshBeginFindsNothing()
{
    const auto dir = makeTestDir("fresh");
    StartupProbe probe(kDeadPid);
    const auto previous = probe.begin(dir, "1.2.3");
    CHECK(!previous.found);
    CHECK(std::filesystem::exists(dir / StartupProbe::markerFileName()));
    CHECK(probe.currentStage() == "begin");
    std::filesystem::remove_all(dir);
}

void testCrashedRunIsReportedOnceWithLastStage()
{
    const auto dir = makeTestDir("crashed");

    // Simulated launch that dies after reaching backend-init: the probe is
    // simply never complete()d, and its recorded pid is dead.
    {
        StartupProbe crashed(kDeadPid);
        (void)crashed.begin(dir, "1.2.3");
        crashed.stage("backend-init");
    }
    CHECK(std::filesystem::exists(dir / StartupProbe::markerFileName()));

    // Next launch sees the stale marker with the last stage and the version.
    StartupProbe next(kDeadPid);
    const auto previous = next.begin(dir, "1.2.4");
    CHECK(previous.found);
    CHECK(previous.stage == "backend-init");
    CHECK(previous.detail.find("version=1.2.3") != std::string::npos);
    CHECK(previous.detail.find("pid=" + std::to_string(kDeadPid)) != std::string::npos);

    // The report is consumed: a third launch only sees the second launch's
    // own marker (also dead pid here), whose stage is "begin", not the
    // original crash stage.
    StartupProbe third(kDeadPid);
    const auto secondPrevious = third.begin(dir, "1.2.4");
    CHECK(secondPrevious.found);
    CHECK(secondPrevious.stage == "begin");
    std::filesystem::remove_all(dir);
}

void testCompleteRemovesMarker()
{
    const auto dir = makeTestDir("complete");
    StartupProbe probe(kDeadPid);
    (void)probe.begin(dir, "1.2.3");
    probe.stage("main-window-create");
    probe.complete();
    CHECK(!std::filesystem::exists(dir / StartupProbe::markerFileName()));

    StartupProbe next(kDeadPid);
    const auto previous = next.begin(dir, "1.2.3");
    CHECK(!previous.found);
    std::filesystem::remove_all(dir);
}

void testLiveInstanceIsNotReported()
{
    const auto dir = makeTestDir("live");

    // Marker written with the real (alive) pid of this test process.
    {
        StartupProbe live; // real pid
        (void)live.begin(dir, "1.2.3");
        live.stage("backend-init");
    }

    StartupProbe next(kDeadPid);
    const auto previous = next.begin(dir, "1.2.3");
    CHECK(!previous.found);
    std::filesystem::remove_all(dir);
}

void testStageAndCompleteBeforeBeginAreNoOps()
{
    StartupProbe probe(kDeadPid);
    probe.stage("anything"); // must not crash or write anywhere
    probe.complete();
    CHECK(probe.currentStage().empty());
}

} // namespace

int main()
{
    mib::test::Watchdog watchdog(20);
    watchdog.mark("start");

    testFreshBeginFindsNothing();
    watchdog.mark("fresh");
    testCrashedRunIsReportedOnceWithLastStage();
    watchdog.mark("crashed");
    testCompleteRemovesMarker();
    watchdog.mark("complete");
    testLiveInstanceIsNotReported();
    watchdog.mark("live");
    testStageAndCompleteBeforeBeginAreNoOps();
    watchdog.mark("noop");

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "startup_probe_test OK\n";
    return 0;
}

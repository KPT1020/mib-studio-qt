// Regression test for CrashReporter's terminate path (local-only mode, the
// configuration the Linux CI lane builds).
//
// Before the fix, an uncaught exception on a worker thread aborted the
// process with an anonymous "terminate" sidecar: the exception message was
// lost. The terminate handler must now write a <ts>-pid<pid>-terminate.txt
// sidecar containing the what() text next to the state-snapshot .json.
//
// The child half re-executes this binary with "child" as argv[1], installs
// the CrashReporter into a scratch crash dir, and lets a worker-thread
// exception escape. The parent asserts on the artifacts. No naked wait: the
// child kills itself via std::terminate immediately, and the parent guards
// with the shared watchdog.

#include "backend/services/CrashReporter.h"

#include "support/watchdog.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

namespace fs = std::filesystem;

namespace {

constexpr const char* kMarker = "boom-crash-reporter-terminate-test";

int runChild(const fs::path& crashDir) {
    backend::services::CrashReporter::Config cfg;
    cfg.crashDir = crashDir;
    cfg.uploadPendingOnStart = false;
    backend::services::CrashReporter::init(cfg);

    std::thread([] {
        throw std::runtime_error(kMarker);
    }).join();

    // Unreachable: the throw above hits std::terminate.
    return 1;
}

bool fileContains(const fs::path& p, const std::string& needle) {
    std::ifstream f(p);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str().find(needle) != std::string::npos;
}

} // namespace

int main(int argc, char* argv[]) {
    const fs::path crashDir =
        fs::temp_directory_path() / "mib_crash_reporter_terminate_test";

    if (argc > 1 && std::string(argv[1]) == "child") {
        return runChild(crashDir);
    }

    mib::test::Watchdog watchdog(30);
    watchdog.mark("spawn child");

    std::error_code ec;
    fs::remove_all(crashDir, ec);
    fs::create_directories(crashDir, ec);

    std::string cmd = std::string("\"") + argv[0] + "\" child";
    const int rc = std::system(cmd.c_str());
    watchdog.mark("child exited");
    std::printf("child exit status: %d (nonzero expected — it aborts)\n", rc);
    if (rc == 0) {
        std::printf("FAIL: child was expected to die via std::terminate\n");
        return 1;
    }

    bool sawJson = false;
    bool sawTxtWithMessage = false;
    for (const auto& entry : fs::directory_iterator(crashDir, ec)) {
        const auto name = entry.path().filename().string();
        if (name.find("-terminate.json") != std::string::npos) {
            sawJson = true;
        }
        if (name.find("-terminate.txt") != std::string::npos &&
            fileContains(entry.path(), kMarker)) {
            sawTxtWithMessage = true;
        }
    }

    int failures = 0;
    if (!sawJson) {
        std::printf("FAIL: no -terminate.json state sidecar in %s\n",
                    crashDir.string().c_str());
        ++failures;
    }
    if (!sawTxtWithMessage) {
        std::printf("FAIL: no -terminate.txt sidecar containing '%s'\n", kMarker);
        ++failures;
    }

    fs::remove_all(crashDir, ec);
    if (failures == 0) {
        std::printf("crash_reporter_terminate_test: passed\n");
        return 0;
    }
    return 1;
}

// Regression test for Logger hardening:
//  1. init() with an unwritable path must fall back to a per-user temp file
//     (a console-only fallback is invisible in the release GUI build).
//  2. init() is idempotent — the first successful call wins, so main() can
//     bring the logger up before AppBackend::initialize() calls it again.
//
// Logger state is process-global, so both properties are asserted in one
// process in order.

#include "backend/services/Logger.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using backend::services::Logger;

int main() {
    int failures = 0;

    // 1. Unwritable target -> fallback file sink, not console-only.
    Logger::init("/proc/definitely/not/writable/app.log");
    const std::string resolved = Logger::resolvedLogFilePath();
    if (resolved.empty()) {
        std::printf("FAIL: file logging silently disabled on unwritable path\n");
        ++failures;
    } else if (resolved == "/proc/definitely/not/writable/app.log") {
        std::printf("FAIL: resolved path claims the unwritable location\n");
        ++failures;
    } else {
        std::printf("PASS: fell back to %s\n", resolved.c_str());
    }

    SPDLOG_INFO("logger_init_fallback_test marker line");
    if (Logger::get()) Logger::get()->flush();

    if (!resolved.empty()) {
        std::ifstream f(resolved);
        std::stringstream ss;
        ss << f.rdbuf();
        if (ss.str().find("logger_init_fallback_test marker line") == std::string::npos) {
            std::printf("FAIL: marker line not present in fallback log file\n");
            ++failures;
        } else {
            std::printf("PASS: fallback log file receives messages\n");
        }
    }

    // 2. Second init must be a no-op (first successful call wins).
    const auto second = fs::temp_directory_path() / "mib_logger_second_init" / "app.log";
    Logger::init(second.string());
    if (Logger::resolvedLogFilePath() != resolved) {
        std::printf("FAIL: second init() replaced the active logger\n");
        ++failures;
    } else {
        std::printf("PASS: init() is idempotent\n");
    }

    std::error_code ec;
    fs::remove_all(second.parent_path(), ec);

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("logger_init_fallback_test: passed\n");
    return 0;
}

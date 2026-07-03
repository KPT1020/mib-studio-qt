#pragma once

#include <memory>
#include <string>
#include <spdlog/spdlog.h>

namespace backend::services {

class Logger {
public:
    // Idempotent: the first successful call wins; later calls are no-ops.
    // This allows main() to bring the file logger up before the
    // CrashReporter while AppBackend::initialize() keeps calling it for
    // tests / embedders that skip main().
    static void init(const std::string& logFilePath);

    // Resolves the log path from a data directory (falling back to
    // %LOCALAPPDATA% on Windows when dataDir is under Program Files) and
    // calls init(). Same idempotency.
    static void initFromDataDir(const std::string& dataDir);

    static std::shared_ptr<spdlog::logger> get();

    // Path of the active log file. Empty when file logging failed and the
    // logger is running console-only.
    static std::string resolvedLogFilePath();
};

} // namespace backend::services

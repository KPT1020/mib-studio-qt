#include "backend/services/Logger.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <vector>
#include <iostream>
#include <exception>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <shlobj.h>
#endif

namespace backend::services {

static std::shared_ptr<spdlog::logger> s_logger;
static std::string s_logFilePath;

static spdlog::level::level_enum defaultLogLevel() {
#ifdef NDEBUG
    return spdlog::level::info;
#else
    return spdlog::level::debug;
#endif
}

namespace {

// Builds the "app" logger around the given file sink plus a console sink
// and installs it as the spdlog default.
void installLogger(spdlog::sink_ptr fileSink, const std::string& logFilePath) {
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    std::vector<spdlog::sink_ptr> sinks;
    if (fileSink) sinks.push_back(fileSink);
    sinks.push_back(console_sink);

    s_logger = std::make_shared<spdlog::logger>("app", sinks.begin(), sinks.end());
    s_logger->set_level(defaultLogLevel());
    spdlog::set_default_logger(s_logger);
    s_logFilePath = fileSink ? logFilePath : std::string{};

    // Flush on every log level to ensure logs are written immediately
    spdlog::flush_on(spdlog::level::trace);
    // Set flush every 3 seconds to ensure logs are written even if app crashes
    spdlog::flush_every(std::chrono::seconds(3));
}

spdlog::sink_ptr makeRotatingSink(const std::string& logFilePath) {
    std::filesystem::path logPath(logFilePath);
    std::filesystem::create_directories(logPath.parent_path());
    // Max file size: 10MB, Max files: 5. rotate_on_open must stay false:
    // rotating on every launch lets a 5-restart crash-loop evict all
    // history before anyone can read it.
    return std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        logFilePath, 10 * 1024 * 1024, 5, false);
}

} // namespace

void Logger::init(const std::string& logFilePath) {
    if (s_logger) {
        return; // already initialized (main() brings the logger up early)
    }
    try {
        installLogger(makeRotatingSink(logFilePath), logFilePath);
        SPDLOG_INFO("Logger initialized: {}", logFilePath);
        s_logger->flush(); // Explicit flush after initialization
        return;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: Failed to initialize file logger for " << logFilePath
                  << ": " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "ERROR: Failed to initialize file logger for " << logFilePath << std::endl;
    }

    // The requested location is unwritable. A console-only logger is
    // invisible in the GUI build, so first try a per-user temp location
    // before degrading to console-only.
    try {
        std::error_code ec;
        const auto tmp = std::filesystem::temp_directory_path(ec);
        if (!ec) {
            const std::string fallback =
                (tmp / "MIB_Studio_Qt" / "logs" / "app.log").string();
            installLogger(makeRotatingSink(fallback), fallback);
            SPDLOG_ERROR("Logger: could not open {}, logging to fallback {}",
                         logFilePath, fallback);
            s_logger->flush();
            return;
        }
    } catch (...) {
        // fall through to console-only
    }

    try {
        installLogger(nullptr, {});
        SPDLOG_ERROR("Logger: file logging unavailable ({}), console only", logFilePath);
    } catch (...) {
        std::cerr << "ERROR: Failed to initialize logger for: " << logFilePath << std::endl;
    }
}

void Logger::initFromDataDir(const std::string& dataDir) {
    if (s_logger) {
        return;
    }
    std::string logPath;
    try {
        std::filesystem::path dataPath(dataDir);
#ifdef _WIN32
        // Installs under Program Files are not user-writable; use
        // %LOCALAPPDATA% instead (mirrors the pre-existing AppBackend logic).
        std::string dataDirLower = dataDir;
        std::transform(dataDirLower.begin(), dataDirLower.end(), dataDirLower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (dataDirLower.find("program files") != std::string::npos) {
            char appDataPath[MAX_PATH];
            if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL,
                                           SHGFP_TYPE_CURRENT, appDataPath))) {
                dataPath = std::filesystem::path(appDataPath) / "MIB_Studio_Qt";
            }
        }
#endif
        logPath = (dataPath / "logs" / "app.log").string();
    } catch (...) {
        logPath = "app.log";
    }
    init(logPath);
}

std::shared_ptr<spdlog::logger> Logger::get() { return s_logger; }

std::string Logger::resolvedLogFilePath() { return s_logFilePath; }

} // namespace backend::services

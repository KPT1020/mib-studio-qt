#include "backend/services/Logger.h"

#include <filesystem>
#include <memory>
#include <vector>
#include <iostream>
#include <exception>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

namespace backend::services {

static std::shared_ptr<spdlog::logger> s_logger;

static spdlog::level::level_enum defaultLogLevel() {
#ifdef NDEBUG
    return spdlog::level::info;
#else
    return spdlog::level::debug;
#endif
}

void Logger::init(const std::string& logFilePath) {
    try {
        std::filesystem::path logPath(logFilePath);
        std::filesystem::create_directories(logPath.parent_path());

        // Use rotating file sink to ensure logs are written and can be rotated
        // Max file size: 10MB, Max files: 5
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logFilePath, 10 * 1024 * 1024, 5, true);
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

        std::vector<spdlog::sink_ptr> sinks{file_sink, console_sink};
        s_logger = std::make_shared<spdlog::logger>("app", sinks.begin(), sinks.end());

        s_logger->set_level(defaultLogLevel());
        spdlog::set_default_logger(s_logger);

        // Flush on every log level to ensure logs are written immediately
        spdlog::flush_on(spdlog::level::trace);

        // Set flush every 3 seconds to ensure logs are written even if app crashes
        spdlog::flush_every(std::chrono::seconds(3));

        SPDLOG_INFO("Logger initialized: {}", logFilePath);
        s_logger->flush(); // Explicit flush after initialization
    } catch (const std::exception& e) {
        // If file logging fails, fall back to console-only logging
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        s_logger = std::make_shared<spdlog::logger>("app", console_sink);
        s_logger->set_level(defaultLogLevel());
        spdlog::set_default_logger(s_logger);
        SPDLOG_ERROR("Failed to initialize file logger ({}), using console only: {}", logFilePath, e.what());
    } catch (...) {
        // Last resort: create a minimal console logger
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        s_logger = std::make_shared<spdlog::logger>("app", console_sink);
        s_logger->set_level(defaultLogLevel());
        spdlog::set_default_logger(s_logger);
        std::cerr << "ERROR: Failed to initialize logger for: " << logFilePath << std::endl;
    }
}

std::shared_ptr<spdlog::logger> Logger::get() { return s_logger; }

} // namespace backend::services

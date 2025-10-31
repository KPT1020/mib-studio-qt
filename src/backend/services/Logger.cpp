#include "backend/services/Logger.h"

#include <filesystem>
#include <memory>
#include <vector>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace backend::services {

static std::shared_ptr<spdlog::logger> s_logger;

void Logger::init(const std::string& logFilePath) {
    std::filesystem::path logPath(logFilePath);
    std::filesystem::create_directories(logPath.parent_path());

    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath, true);
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

    std::vector<spdlog::sink_ptr> sinks{file_sink, console_sink};
    s_logger = std::make_shared<spdlog::logger>("app", sinks.begin(), sinks.end());

    s_logger->set_level(spdlog::level::info);
    spdlog::set_default_logger(s_logger);
    spdlog::flush_on(spdlog::level::info);

    SPDLOG_INFO("Logger initialized: {}", logFilePath);
}

std::shared_ptr<spdlog::logger> Logger::get() { return s_logger; }

} // namespace backend::services

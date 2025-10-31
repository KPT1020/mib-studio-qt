#pragma once

#include <memory>
#include <string>
#include <spdlog/spdlog.h>

namespace backend::services {

class Logger {
public:
    static void init(const std::string& logFilePath);
    static std::shared_ptr<spdlog::logger> get();
};

} // namespace backend::services

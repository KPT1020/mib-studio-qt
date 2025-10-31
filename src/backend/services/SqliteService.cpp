#include "backend/services/SqliteService.h"
#include "backend/services/Logger.h"

#include <spdlog/spdlog.h>

namespace backend::services {

bool SqliteService::initialize(const std::string& dbPath) {
    SPDLOG_INFO("SqliteService initialized at {}", dbPath);
    return true;
}

bool SqliteService::execute(const std::string& sql) {
    SPDLOG_INFO("SqliteService execute: {}", sql);
    return true;
}

} // namespace backend::services

#pragma once

#include <string>

namespace backend::services {

class SqliteService {
public:
    bool initialize(const std::string& dbPath);
    bool execute(const std::string& sql);
};

} // namespace backend::services

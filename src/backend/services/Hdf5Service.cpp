#include "backend/services/Hdf5Service.h"
#include "backend/services/Logger.h"

#include <spdlog/spdlog.h>

namespace backend::services {

bool Hdf5Service::initialize(const std::string& rootDir) {
    SPDLOG_INFO("Hdf5Service initialized at {}", rootDir);
    return true;
}

bool Hdf5Service::writeDataset(const std::string& name) {
    SPDLOG_INFO("Hdf5Service writeDataset: {}", name);
    return true;
}

} // namespace backend::services

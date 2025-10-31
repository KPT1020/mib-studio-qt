#pragma once

#include <string>

namespace backend::services {

class Hdf5Service {
public:
    bool initialize(const std::string& rootDir);
    bool writeDataset(const std::string& name);
};

} // namespace backend::services

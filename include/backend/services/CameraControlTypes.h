#pragma once

#include <string>
#include <vector>

namespace backend::services {

struct DiscoveredCamera {
    int interfaceIndex = -1;
    int deviceIndex = -1;
    std::string interfaceID;
    std::string deviceID;
    std::string modelName;
    std::string firmwareVersion;
    std::string label;
};

struct DiscoveredFramegrabber {
    int interfaceIndex = -1;
    int deviceIndex = -1;
    int streamIndex = -1;
    std::string interfaceID;
    std::string deviceID;
    std::string streamID;
    std::string modelName;
    std::string label;
};

} // namespace backend::services

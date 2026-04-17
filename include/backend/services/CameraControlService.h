#pragma once

#ifdef MIB_HAS_EGRABBER
#include <EGrabber.h>
#endif

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

class CameraControlService {
public:
    CameraControlService() = default;
    ~CameraControlService() = default;

#ifdef MIB_HAS_EGRABBER
    std::vector<DiscoveredCamera> discoverCameras();
    std::vector<DiscoveredFramegrabber> discoverFramegrabbers();

    bool applyScriptToDevice(int interfaceIndex,
                             int deviceIndex,
                             const std::string& scriptPath,
                             std::string* errorOut = nullptr);

    bool deviceReset(int interfaceIndex,
                     int deviceIndex,
                     std::string* errorOut = nullptr);
#else
    std::vector<DiscoveredCamera> discoverCameras() { return {}; }
    std::vector<DiscoveredFramegrabber> discoverFramegrabbers() { return {}; }

    bool applyScriptToDevice(int, int, const std::string&, std::string* = nullptr) { return false; }
    bool deviceReset(int, int, std::string* = nullptr) { return false; }
#endif
};

} // namespace backend::services




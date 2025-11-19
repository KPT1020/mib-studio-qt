#pragma once

#include <EGrabber.h>

#include <optional>
#include <string>
#include <vector>

namespace backend::services {

struct DiscoveredCamera {
    int interfaceIndex = -1;
    int deviceIndex = -1;
    std::string interfaceID;
    std::string deviceID;
    std::string modelName;
    std::string label; // interfaceID/deviceID (model)
};

/**
 * Camera utility service for:
 *  - Enumerating available cameras
 *  - Applying a JS configuration script to a specific device
 *
 * Note: This service does not own or interact with the CaptureService thread.
 */
class CameraControlService {
public:
    CameraControlService() = default;
    ~CameraControlService() = default;

    std::vector<DiscoveredCamera> discoverCameras();

    bool applyScriptToDevice(int interfaceIndex,
                             int deviceIndex,
                             const std::string& scriptPath,
                             std::string* errorOut = nullptr);

    // Issue GenICam SFNC DeviceReset to a specific device.
    // Best effort stops acquisition first; returns true on success.
    bool deviceReset(int interfaceIndex,
                     int deviceIndex,
                     std::string* errorOut = nullptr);
};

} // namespace backend::services




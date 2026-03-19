#pragma once

// Note: <EGrabber.h> is included only in the .cpp to avoid polluting
// MindVision-related code with eGrabber headers.

#include <optional>
#include <string>
#include <vector>

namespace backend::services {

enum class CameraType {
    EGrabber,
    MindVision,
};

struct DiscoveredCamera {
    CameraType cameraType = CameraType::EGrabber;

    // EGrabber fields (valid when cameraType == EGrabber)
    int interfaceIndex = -1;
    int deviceIndex    = -1;
    std::string interfaceID;
    std::string deviceID;
    std::string firmwareVersion; // "Unknown" if not available

    // MindVision field (valid when cameraType == MindVision)
    int cameraIndex = -1; // index into CameraEnumerateDevice list

    std::string modelName;
    std::string label; // human-readable display string
};

struct DiscoveredFramegrabber {
    int interfaceIndex = -1;
    int deviceIndex    = -1;
    int streamIndex    = -1;
    std::string interfaceID;
    std::string deviceID;
    std::string streamID;
    std::string modelName;
    std::string label; // interfaceID/deviceID/streamID (model)
};

/**
 * Camera utility service for:
 *  - Enumerating available cameras (eGrabber and/or MindVision)
 *  - Applying a JS configuration script to a specific eGrabber device
 *
 * Note: This service does not own or interact with the CaptureService thread.
 */
class CameraControlService {
public:
    CameraControlService() = default;
    ~CameraControlService() = default;

    std::vector<DiscoveredCamera> discoverCameras();
    std::vector<DiscoveredCamera> discoverMindVisionCameras();
    std::vector<DiscoveredCamera> discoverAllCameras();

    std::vector<DiscoveredFramegrabber> discoverFramegrabbers();

    bool applyScriptToDevice(int interfaceIndex,
                             int deviceIndex,
                             const std::string& scriptPath,
                             std::string* errorOut = nullptr);

    // Issue GenICam SFNC DeviceReset to a specific eGrabber device.
    // Best effort stops acquisition first; returns true on success.
    bool deviceReset(int interfaceIndex,
                     int deviceIndex,
                     std::string* errorOut = nullptr);

    // Open a MindVision camera temporarily and apply settings from a JSON config file.
    // The camera must not already be open (capture must be stopped before calling).
    bool applyMindVisionConfig(int cameraIndex,
                               const std::string& jsonPath,
                               std::string* errorOut = nullptr);
};

} // namespace backend::services

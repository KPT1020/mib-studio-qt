#include "backend/services/CameraControlService.h"

#include <spdlog/spdlog.h>

#include <sstream>

using namespace Euresys;

namespace backend::services {

std::vector<DiscoveredCamera> CameraControlService::discoverCameras() {
    std::vector<DiscoveredCamera> results;
    try {
        EGenTL genTL;
        gc::TL_HANDLE tl = genTL.tlOpen();
        const uint32_t numInterfaces = genTL.tlGetNumInterfaces(tl);
        for (uint32_t ifIdx = 0; ifIdx < numInterfaces; ++ifIdx) {
            std::string interfaceID = genTL.tlGetInterfaceID(tl, ifIdx);
            gc::IF_HANDLE ifHandle = genTL.tlOpenInterface(tl, interfaceID);
            const uint32_t numDevices = genTL.ifGetNumDevices(ifHandle);
            for (uint32_t devIdx = 0; devIdx < numDevices; ++devIdx) {
                std::string deviceID = genTL.ifGetDeviceID(ifHandle, devIdx);

                // Try to query model name by temporarily opening an EGrabber
                std::string modelName = "Unknown";
                try {
                    EGrabber<CallbackOnDemand> probe(genTL, static_cast<int>(ifIdx), static_cast<int>(devIdx));
                    try {
                        modelName = probe.getString<DeviceModule>("DeviceModelName");
                    } catch (const gentl_error&) {
                        // ignore if not available
                    }
                } catch (const std::exception& ex) {
                    SPDLOG_WARN("CameraControlService: probe open failed for {}/{}: {}", interfaceID, deviceID, ex.what());
                }

                DiscoveredCamera dc{};
                dc.interfaceIndex = static_cast<int>(ifIdx);
                dc.deviceIndex = static_cast<int>(devIdx);
                dc.interfaceID = interfaceID;
                dc.deviceID = deviceID;
                dc.modelName = modelName;
                {
                    std::ostringstream oss;
                    oss << interfaceID << "/" << deviceID;
                    if (!modelName.empty() && modelName != "Unknown") {
                        oss << " (" << modelName << ")";
                    }
                    dc.label = oss.str();
                }
                results.push_back(std::move(dc));
            }
            genTL.ifClose(ifHandle);
        }
        genTL.tlClose(tl);
        SPDLOG_INFO("Discovery found {} device(s)", results.size());
    } catch (const std::exception& ex) {
        SPDLOG_ERROR("CameraControlService::discoverCameras failed: {}", ex.what());
        results.clear();
    }
    return results;
}

bool CameraControlService::applyScriptToDevice(int interfaceIndex,
                                               int deviceIndex,
                                               const std::string& scriptPath,
                                               std::string* errorOut) {
    try {
        EGenTL genTL;
        EGrabber<CallbackOnDemand> g(genTL, interfaceIndex, deviceIndex);

        SPDLOG_INFO("Applying script to camera [{}:{}]: {}", interfaceIndex, deviceIndex, scriptPath);
        g.runScript(scriptPath);

        // Safety: ensure acquisition is stopped after script application
        try {
            g.execute<RemoteModule>("AcquisitionStop");
        } catch (const std::exception& ex) {
            SPDLOG_WARN("AcquisitionStop after script failed: {}", ex.what());
        }
        SPDLOG_INFO("Script applied successfully");
        return true;
    } catch (const std::exception& ex) {
        SPDLOG_ERROR("applyScriptToDevice failed: {}", ex.what());
        if (errorOut) {
            *errorOut = ex.what();
        }
        return false;
    }
}

} // namespace backend::services




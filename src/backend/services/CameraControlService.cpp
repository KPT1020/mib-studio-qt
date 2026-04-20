#include "backend/services/CameraControlService.h"

#include <spdlog/spdlog.h>

#include <sstream>

#ifndef MIB_HAS_EGRABBER
#define MIB_HAS_EGRABBER 0
#endif

#if MIB_HAS_EGRABBER
#include <EGrabber.h>
using namespace Euresys;
#endif

namespace backend::services
{
#if MIB_HAS_EGRABBER

    std::vector<DiscoveredCamera> CameraControlService::discoverCameras()
    {
        std::vector<DiscoveredCamera> results;
        try
        {
            EGenTL genTL;
            EGrabberDiscovery discovery(genTL);
            discovery.discover(); // Discover cameras (default behavior)

            for (int i = 0; i < discovery.cameraCount(); ++i)
            {
                EGrabberCameraInfo cameraInfo = discovery.cameras(i);

                // Use the first grabber from the camera (master device for multi-bank cameras)
                if (cameraInfo.grabbers.empty())
                {
                    continue;
                }

                const EGrabberInfo &grabberInfo = cameraInfo.grabbers[0];

                // EGrabberInfo provides indices directly
                int interfaceIndex = grabberInfo.interfaceIndex;
                int deviceIndex = grabberInfo.deviceIndex;

                // Try to query model name and firmware version by temporarily opening an EGrabber
                std::string modelName = grabberInfo.isRemoteAvailable ? grabberInfo.deviceModelName : "Unknown";
                std::string firmwareVersion = "Unknown";

                try
                {
                    EGrabber<CallbackOnDemand> probe(genTL, interfaceIndex, deviceIndex);
                    try
                    {
                        if (modelName == "Unknown" || modelName.empty())
                        {
                            modelName = probe.getString<DeviceModule>("DeviceModelName");
                        }
                    }
                    catch (const gentl_error &)
                    {
                        // ignore if not available
                    }
                    try
                    {
                        firmwareVersion = probe.getString<DeviceModule>("DeviceVersion");
                    }
                    catch (const gentl_error &)
                    {
                        // ignore if not available
                    }
                }
                catch (const std::exception &ex)
                {
                    SPDLOG_WARN("CameraControlService: probe open failed for camera {}/{}: {}",
                                grabberInfo.interfaceID, grabberInfo.deviceID, ex.what());
                }

                DiscoveredCamera dc{};
                dc.interfaceIndex = interfaceIndex;
                dc.deviceIndex = deviceIndex;
                dc.interfaceID = grabberInfo.interfaceID;
                dc.deviceID = grabberInfo.deviceID;
                dc.modelName = modelName;
                dc.firmwareVersion = firmwareVersion;
                {
                    std::ostringstream oss;
                    oss << grabberInfo.interfaceID << "/" << grabberInfo.deviceID;
                    if (!modelName.empty() && modelName != "Unknown")
                    {
                        oss << " (" << modelName << ")";
                    }
                    if (!firmwareVersion.empty() && firmwareVersion != "Unknown")
                    {
                        oss << " [Firmware: " << firmwareVersion << "]";
                    }
                    dc.label = oss.str();
                }
                results.push_back(std::move(dc));
            }

            SPDLOG_INFO("Discovery found {} camera(s)", results.size());
        }
        catch (const std::exception &ex)
        {
            SPDLOG_ERROR("CameraControlService::discoverCameras failed: {}", ex.what());
            results.clear();
        }
        return results;
    }

    std::vector<DiscoveredFramegrabber> CameraControlService::discoverFramegrabbers()
    {
        std::vector<DiscoveredFramegrabber> results;
        try
        {
            EGenTL genTL;
            EGrabberDiscovery discovery(genTL);
            discovery.discover(); // Discover both eGrabbers and cameras

            for (int i = 0; i < discovery.egrabberCount(); ++i)
            {
                EGrabberInfo grabberInfo = discovery.egrabbers(i);

                // EGrabberInfo provides indices directly
                int interfaceIndex = grabberInfo.interfaceIndex;
                int deviceIndex = grabberInfo.deviceIndex;
                int streamIndex = grabberInfo.streamIndex;

                // Try to query model name by temporarily opening an EGrabber
                std::string modelName = grabberInfo.isRemoteAvailable ? grabberInfo.deviceModelName : "Unknown";

                try
                {
                    EGrabber<CallbackOnDemand> probe(genTL, interfaceIndex, deviceIndex);
                    try
                    {
                        if (modelName == "Unknown" || modelName.empty())
                        {
                            modelName = probe.getString<DeviceModule>("DeviceModelName");
                        }
                    }
                    catch (const gentl_error &)
                    {
                        // ignore if not available
                    }
                }
                catch (const std::exception &ex)
                {
                    SPDLOG_WARN("CameraControlService: probe open failed for framegrabber {}/{}/{}: {}",
                                grabberInfo.interfaceID, grabberInfo.deviceID, grabberInfo.streamID, ex.what());
                }

                DiscoveredFramegrabber df{};
                df.interfaceIndex = interfaceIndex;
                df.deviceIndex = deviceIndex;
                df.streamIndex = streamIndex;
                df.interfaceID = grabberInfo.interfaceID;
                df.deviceID = grabberInfo.deviceID;
                df.streamID = grabberInfo.streamID;
                df.modelName = modelName;
                {
                    std::ostringstream oss;
                    oss << grabberInfo.interfaceID << "/" << grabberInfo.deviceID << "/" << grabberInfo.streamID;
                    if (!modelName.empty() && modelName != "Unknown")
                    {
                        oss << " (" << modelName << ")";
                    }
                    df.label = oss.str();
                }
                results.push_back(std::move(df));
            }

            SPDLOG_INFO("Discovery found {} framegrabber(s)", results.size());
        }
        catch (const std::exception &ex)
        {
            SPDLOG_ERROR("CameraControlService::discoverFramegrabbers failed: {}", ex.what());
            results.clear();
        }
        return results;
    }

    bool CameraControlService::applyScriptToDevice(int interfaceIndex,
                                                   int deviceIndex,
                                                   const std::string &scriptPath,
                                                   std::string *errorOut)
    {
        try
        {
            EGenTL genTL;
            EGrabber<CallbackOnDemand> g(genTL, interfaceIndex, deviceIndex);

            SPDLOG_INFO("Applying script to camera [{}:{}]: {}", interfaceIndex, deviceIndex, scriptPath);
            g.runScript(scriptPath);

            // Safety: ensure acquisition is stopped after script application
            try
            {
                g.execute<RemoteModule>("AcquisitionStop");
            }
            catch (const std::exception &ex)
            {
                SPDLOG_WARN("AcquisitionStop after script failed: {}", ex.what());
            }
            SPDLOG_INFO("Script applied successfully");
            return true;
        }
        catch (const std::exception &ex)
        {
            SPDLOG_ERROR("applyScriptToDevice failed: {}", ex.what());
            if (errorOut)
            {
                *errorOut = ex.what();
            }
            return false;
        }
    }

    bool CameraControlService::deviceReset(int interfaceIndex,
                                           int deviceIndex,
                                           std::string *errorOut)
    {
        try
        {
            EGenTL genTL;
            EGrabber<CallbackOnDemand> g(genTL, interfaceIndex, deviceIndex);

            // Best-effort: stop acquisition before reset
            try
            {
                g.execute<RemoteModule>("AcquisitionStop");
            }
            catch (const std::exception &ex)
            {
                SPDLOG_WARN("AcquisitionStop before reset failed: {}", ex.what());
            }

            SPDLOG_INFO("Issuing DeviceReset to camera [{}:{}]", interfaceIndex, deviceIndex);
            g.execute<DeviceModule>("DeviceReset");
            SPDLOG_INFO("DeviceReset command sent");
            return true;
        }
        catch (const std::exception &ex)
        {
            SPDLOG_ERROR("deviceReset failed: {}", ex.what());
            if (errorOut)
            {
                *errorOut = ex.what();
            }
            return false;
        }
    }

#else
    namespace
    {
        bool reportUnsupported(const char *operation, std::string *errorOut)
        {
            static bool loggedOnce = false;
            if (!loggedOnce)
            {
                SPDLOG_WARN("CameraControlService hardware operations unavailable on this platform (EGrabber SDK is Windows-only)");
                loggedOnce = true;
            }
            if (errorOut)
            {
                *errorOut = "EGrabber SDK is unavailable on this platform";
            }
            (void)operation;
            return false;
        }
    } // namespace

    std::vector<DiscoveredCamera> CameraControlService::discoverCameras()
    {
        reportUnsupported("discoverCameras", nullptr);
        return {};
    }

    std::vector<DiscoveredFramegrabber> CameraControlService::discoverFramegrabbers()
    {
        reportUnsupported("discoverFramegrabbers", nullptr);
        return {};
    }

    bool CameraControlService::applyScriptToDevice(int interfaceIndex,
                                                   int deviceIndex,
                                                   const std::string &scriptPath,
                                                   std::string *errorOut)
    {
        (void)interfaceIndex;
        (void)deviceIndex;
        (void)scriptPath;
        return reportUnsupported("applyScriptToDevice", errorOut);
    }

    bool CameraControlService::deviceReset(int interfaceIndex,
                                           int deviceIndex,
                                           std::string *errorOut)
    {
        (void)interfaceIndex;
        (void)deviceIndex;
        return reportUnsupported("deviceReset", errorOut);
    }

#endif
} // namespace backend::services

#include "backend/services/CameraControlService.h"

// EGrabber SDK – included here (not in the header) to avoid polluting
// MindVision-related compilation units with eGrabber headers.
#include <EGrabber.h>

// MindVision SDK – extern declarations only (API_LOAD_MAIN is defined in
// MindVisionCamera.cpp which provides all global definitions).
#ifdef _WIN32
#include <windows.h>
#endif
#include "MindVision/CameraApiLoad.h"

#include <spdlog/spdlog.h>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <sstream>

using namespace Euresys;

namespace backend::services
{

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

    std::vector<DiscoveredCamera> CameraControlService::discoverMindVisionCameras()
    {
        std::vector<DiscoveredCamera> results;
        try
        {
            // Load the MindVision SDK DLL (idempotent)
            if (LoadSdkApi() != CAMERA_STATUS_SUCCESS)
            {
                SPDLOG_WARN("CameraControlService::discoverMindVisionCameras: SDK DLL not available");
                return results;
            }

            CameraSdkStatus status = CameraSdkInit(0);
            if (status != CAMERA_STATUS_SUCCESS)
            {
                SPDLOG_WARN("CameraControlService: CameraSdkInit returned {}", status);
            }

            tSdkCameraDevInfo devList[32];
            INT count = 32;
            status = CameraEnumerateDevice(devList, &count);
            if (status != CAMERA_STATUS_SUCCESS)
            {
                SPDLOG_WARN("CameraControlService: CameraEnumerateDevice returned {}", status);
                return results;
            }

            for (int i = 0; i < count; ++i)
            {
                const tSdkCameraDevInfo& info = devList[i];

                DiscoveredCamera dc{};
                dc.cameraType  = CameraType::MindVision;
                dc.cameraIndex = i;
                dc.modelName   = info.acProductName[0] != '\0' ? info.acProductName : "Unknown";

                std::ostringstream oss;
                oss << "[MV] " << dc.modelName;
                if (info.acFriendlyName[0] != '\0' && std::string(info.acFriendlyName) != dc.modelName)
                {
                    oss << " (" << info.acFriendlyName << ")";
                }
                if (info.acSn[0] != '\0')
                {
                    oss << " [SN: " << info.acSn << "]";
                }
                dc.label = oss.str();

                results.push_back(std::move(dc));
            }

            SPDLOG_INFO("MindVision discovery found {} camera(s)", results.size());
        }
        catch (const std::exception& ex)
        {
            SPDLOG_ERROR("CameraControlService::discoverMindVisionCameras failed: {}", ex.what());
            results.clear();
        }
        return results;
    }

    std::vector<DiscoveredCamera> CameraControlService::discoverAllCameras()
    {
        auto cameras = discoverCameras();
        auto mvCameras = discoverMindVisionCameras();
        cameras.insert(cameras.end(),
                       std::make_move_iterator(mvCameras.begin()),
                       std::make_move_iterator(mvCameras.end()));
        return cameras;
    }

    bool CameraControlService::applyMindVisionConfig(int cameraIndex,
                                                     const std::string& jsonPath,
                                                     std::string* errorOut)
    {
        auto setErr = [&](const std::string& msg) {
            if (errorOut) {
                *errorOut = msg;
            }
        };

        // Load SDK (idempotent)
        if (LoadSdkApi() != CAMERA_STATUS_SUCCESS)
        {
            setErr("MindVision SDK DLL not available");
            return false;
        }

        CameraSdkStatus status = CameraSdkInit(0);
        if (status != CAMERA_STATUS_SUCCESS)
        {
            SPDLOG_WARN("applyMindVisionConfig: CameraSdkInit returned {}", status);
        }

        // Enumerate devices
        tSdkCameraDevInfo devList[32];
        INT count = 32;
        status = CameraEnumerateDevice(devList, &count);
        if (status != CAMERA_STATUS_SUCCESS || count == 0)
        {
            setErr("CameraEnumerateDevice failed (status=" + std::to_string(status) + ", count=" + std::to_string(count) + ")");
            return false;
        }
        if (cameraIndex < 0 || cameraIndex >= count)
        {
            setErr("Camera index out of range");
            return false;
        }

        // Open camera
        CameraHandle hCamera = -1;
        status = CameraInit(&devList[cameraIndex], -1, -1, &hCamera);
        if (status != CAMERA_STATUS_SUCCESS)
        {
            setErr("CameraInit failed (status=" + std::to_string(status) + ")");
            return false;
        }

        bool ok = true;

        // Read JSON config file
        QFile f(QString::fromStdString(jsonPath));
        if (!f.open(QIODevice::ReadOnly))
        {
            CameraUnInit(hCamera);
            setErr("Failed to open JSON file: " + jsonPath);
            return false;
        }
        QJsonParseError parseErr;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseErr);
        f.close();
        if (doc.isNull())
        {
            CameraUnInit(hCamera);
            setErr("JSON parse error: " + parseErr.errorString().toStdString());
            return false;
        }
        QJsonObject obj = doc.object();

        int    width          = obj.value("width").toInt(512);
        int    height         = obj.value("height").toInt(96);
        int    offset_x       = obj.value("offset_x").toInt(0);
        int    offset_y       = obj.value("offset_y").toInt(0);
        double expUs          = obj.value("exposure_time_us").toDouble(3000.0);
        int    triggerMode    = obj.value("trigger_mode").toInt(0);
        int    analogGain     = obj.value("analog_gain").toInt(1);
        bool   aeEnabled      = obj.value("auto_exposure_enabled").toBool(false);
        int    aeTarget       = obj.value("ae_target_brightness").toInt(100);
        int    gamma          = obj.value("gamma").toInt(100);
        int    contrast       = obj.value("contrast").toInt(100);
        int    sharpness      = obj.value("sharpness").toInt(0);
        int    frameSpeed     = obj.value("frame_speed").toInt(2);
        bool   flipH          = obj.value("flip_horizontal").toBool(false);
        bool   flipV          = obj.value("flip_vertical").toBool(false);
        int    strobeMode     = obj.value("strobe_mode").toInt(0);
        int    strobePulseUs  = obj.value("strobe_pulse_width_us").toInt(500);
        int    strobeDelayUs  = obj.value("strobe_delay_us").toInt(0);
        int    strobePolarity = obj.value("strobe_polarity").toInt(1);

        SPDLOG_INFO("applyMindVisionConfig: w={} h={} ox={} oy={} exp={} trig={} gain={} ae={} gamma={} speed={}",
                    width, height, offset_x, offset_y, expUs, triggerMode, analogGain, aeEnabled, gamma, frameSpeed);

        // Apply resolution with custom ROI (iIndex=0xFF)
        tSdkImageResolution res{};
        res.iIndex      = 0xFF;
        res.iHOffsetFOV = offset_x;
        res.iVOffsetFOV = offset_y;
        res.iWidthFOV   = width;
        res.iHeightFOV  = height;
        res.iWidth      = width;
        res.iHeight     = height;

        status = CameraSetImageResolution(hCamera, &res);
        if (status != CAMERA_STATUS_SUCCESS)
        {
            SPDLOG_WARN("applyMindVisionConfig: CameraSetImageResolution returned {}", status);
            ok = false;
            setErr("CameraSetImageResolution failed (status=" + std::to_string(status) + ")");
        }

        status = CameraSetExposureTime(hCamera, expUs);
        if (status != CAMERA_STATUS_SUCCESS)
        {
            SPDLOG_WARN("applyMindVisionConfig: CameraSetExposureTime returned {}", status);
            ok = false;
            if (errorOut && errorOut->empty())
            {
                setErr("CameraSetExposureTime failed (status=" + std::to_string(status) + ")");
            }
        }

        status = CameraSetTriggerMode(hCamera, triggerMode);
        if (status != CAMERA_STATUS_SUCCESS)
        {
            SPDLOG_WARN("applyMindVisionConfig: CameraSetTriggerMode returned {}", status);
            ok = false;
            if (errorOut && errorOut->empty())
            {
                setErr("CameraSetTriggerMode failed (status=" + std::to_string(status) + ")");
            }
        }

        status = CameraSetAnalogGain(hCamera, analogGain);
        if (status != CAMERA_STATUS_SUCCESS)
        {
            SPDLOG_WARN("applyMindVisionConfig: CameraSetAnalogGain returned {}", status);
            ok = false;
            if (errorOut && errorOut->empty())
            {
                setErr("CameraSetAnalogGain failed (status=" + std::to_string(status) + ")");
            }
        }

        status = CameraSetAeState(hCamera, aeEnabled ? TRUE : FALSE);
        if (status != CAMERA_STATUS_SUCCESS)
            SPDLOG_WARN("applyMindVisionConfig: CameraSetAeState returned {}", status);

        status = CameraSetAeTarget(hCamera, aeTarget);
        if (status != CAMERA_STATUS_SUCCESS)
            SPDLOG_WARN("applyMindVisionConfig: CameraSetAeTarget returned {}", status);

        status = CameraSetGamma(hCamera, gamma);
        if (status != CAMERA_STATUS_SUCCESS)
            SPDLOG_WARN("applyMindVisionConfig: CameraSetGamma returned {}", status);

        status = CameraSetContrast(hCamera, contrast);
        if (status != CAMERA_STATUS_SUCCESS)
            SPDLOG_WARN("applyMindVisionConfig: CameraSetContrast returned {}", status);

        status = CameraSetSharpness(hCamera, sharpness);
        if (status != CAMERA_STATUS_SUCCESS)
            SPDLOG_WARN("applyMindVisionConfig: CameraSetSharpness returned {}", status);

        status = CameraSetFrameSpeed(hCamera, frameSpeed);
        if (status != CAMERA_STATUS_SUCCESS)
            SPDLOG_WARN("applyMindVisionConfig: CameraSetFrameSpeed returned {}", status);

        status = CameraSetMirror(hCamera, 0, flipH ? TRUE : FALSE);
        if (status != CAMERA_STATUS_SUCCESS)
            SPDLOG_WARN("applyMindVisionConfig: CameraSetMirror(H) returned {}", status);

        status = CameraSetMirror(hCamera, 1, flipV ? TRUE : FALSE);
        if (status != CAMERA_STATUS_SUCCESS)
            SPDLOG_WARN("applyMindVisionConfig: CameraSetMirror(V) returned {}", status);

        status = CameraSetStrobeMode(hCamera, strobeMode);
        if (status != CAMERA_STATUS_SUCCESS)
            SPDLOG_WARN("applyMindVisionConfig: CameraSetStrobeMode returned {}", status);

        status = CameraSetStrobePulseWidth(hCamera, static_cast<UINT>(strobePulseUs));
        if (status != CAMERA_STATUS_SUCCESS)
            SPDLOG_WARN("applyMindVisionConfig: CameraSetStrobePulseWidth returned {}", status);

        status = CameraSetStrobeDelayTime(hCamera, static_cast<UINT>(strobeDelayUs));
        if (status != CAMERA_STATUS_SUCCESS)
            SPDLOG_WARN("applyMindVisionConfig: CameraSetStrobeDelayTime returned {}", status);

        status = CameraSetStrobePolarity(hCamera, strobePolarity);
        if (status != CAMERA_STATUS_SUCCESS)
            SPDLOG_WARN("applyMindVisionConfig: CameraSetStrobePolarity returned {}", status);

        CameraUnInit(hCamera);
        if (ok)
        {
            SPDLOG_INFO("applyMindVisionConfig: config applied from {} to camera index {}", jsonPath, cameraIndex);
        }
        else
        {
            SPDLOG_ERROR("applyMindVisionConfig: config apply failed for {} (camera index {})", jsonPath, cameraIndex);
        }
        return ok;
    }

} // namespace backend::services

#include "backend/camera/mindvision/MindVisionConfigApply.h"

#ifndef MIB_HAS_MINDVISION
#define MIB_HAS_MINDVISION 0
#endif

#if MIB_HAS_MINDVISION

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stdio.h>
#endif

// No API_LOAD_MAIN here: the SDK function-pointer definitions live in
// MindVisionCamera.cpp; this TU links against them (same pattern as
// CameraControlService.cpp).
#if __has_include(<MindVision/CameraApiLoad.h>)
#include <MindVision/CameraApiLoad.h>
#elif __has_include(<CameraApiLoad.h>)
#include <CameraApiLoad.h>
#else
#error "MindVision CameraApiLoad.h not found"
#endif

#endif // MIB_HAS_MINDVISION

#include <QFile>
#include <QString>

#include <spdlog/spdlog.h>

namespace backend::camera::mindvision {

static_assert(kConfigFieldCount == 20,
              "Config changed: extend applyConfigToCamera's SDK setter sequence "
              "for the new field, then update this assert");

#if MIB_HAS_MINDVISION

ApplyResult applyConfigToCamera(int hCamera, const Config& config)
{
    ApplyResult r;
    r.config = config;

    auto check = [&](CameraSdkStatus status, const char* call) {
        if (status != CAMERA_STATUS_SUCCESS) {
            std::string warning = std::string("MindVision apply: ") + call +
                                  " returned " + std::to_string(status);
            SPDLOG_WARN("{}", warning);
            r.warnings.push_back(std::move(warning));
        }
    };

    SPDLOG_INFO("MindVision apply: w={} h={} ox={} oy={} exp={} trig={} gain={}",
                config.width, config.height, config.offsetX, config.offsetY,
                config.exposureUs, config.triggerMode, config.analogGain);

    tSdkImageResolution res{};
    res.iIndex = 0xFF;
    res.iHOffsetFOV = config.offsetX;
    res.iVOffsetFOV = config.offsetY;
    res.iWidthFOV = config.width;
    res.iHeightFOV = config.height;
    res.iWidth = config.width;
    res.iHeight = config.height;

    check(CameraSetImageResolution(hCamera, &res), "CameraSetImageResolution");
    check(CameraSetExposureTime(hCamera, config.exposureUs), "CameraSetExposureTime");
    check(CameraSetTriggerMode(hCamera, config.triggerMode), "CameraSetTriggerMode");
    check(CameraSetAnalogGain(hCamera, config.analogGain), "CameraSetAnalogGain");
    check(CameraSetAeState(hCamera, config.aeEnabled ? TRUE : FALSE), "CameraSetAeState");
    check(CameraSetAeTarget(hCamera, config.aeTarget), "CameraSetAeTarget");
    check(CameraSetGamma(hCamera, config.gamma), "CameraSetGamma");
    check(CameraSetContrast(hCamera, config.contrast), "CameraSetContrast");
    check(CameraSetSharpness(hCamera, config.sharpness), "CameraSetSharpness");
    check(CameraSetFrameSpeed(hCamera, config.frameSpeed), "CameraSetFrameSpeed");
    check(CameraSetMirror(hCamera, 0, config.flipHorizontal ? TRUE : FALSE), "CameraSetMirror(H)");
    check(CameraSetMirror(hCamera, 1, config.flipVertical ? TRUE : FALSE), "CameraSetMirror(V)");
    check(CameraSetStrobeMode(hCamera, config.strobeMode), "CameraSetStrobeMode");
    check(CameraSetStrobePulseWidth(hCamera, static_cast<UINT>(config.strobePulseUs)), "CameraSetStrobePulseWidth");
    check(CameraSetStrobeDelayTime(hCamera, static_cast<UINT>(config.strobeDelayUs)), "CameraSetStrobeDelayTime");
    check(CameraSetStrobePolarity(hCamera, config.strobePolarity), "CameraSetStrobePolarity");
    // triggerOutputIndex is not an SDK setter: MindVisionCamera reads it from
    // ApplyResult::config when TriggerService configures the output line.

    r.ok = true;
    return r;
}

#else

ApplyResult applyConfigToCamera(int hCamera, const Config& config)
{
    (void)hCamera;
    ApplyResult r;
    r.config = config;
    r.error = "MindVision SDK is disabled at build time";
    SPDLOG_WARN("MindVision apply: {}", r.error);
    return r;
}

#endif // MIB_HAS_MINDVISION

ApplyResult applyJsonFileToCamera(int hCamera, const std::string& jsonPath)
{
    QFile file(QString::fromStdString(jsonPath));
    if (!file.open(QIODevice::ReadOnly)) {
        ApplyResult r;
        r.error = "Failed to open MindVision config file: " + jsonPath;
        return r;
    }

    const QByteArray bytes = file.readAll();
    file.close();

    const auto parsed = parseConfig(bytes);
    if (!parsed.ok) {
        ApplyResult r;
        r.error = parsed.error + " (in " + jsonPath + ")";
        return r;
    }

    ApplyResult r = applyConfigToCamera(hCamera, parsed.config);
    r.warnings.insert(r.warnings.begin(), parsed.warnings.begin(), parsed.warnings.end());
    return r;
}

} // namespace backend::camera::mindvision

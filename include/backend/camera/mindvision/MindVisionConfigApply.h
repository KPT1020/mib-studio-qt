// Single shared "apply the JSON config to an open camera handle" path, used by
// both MindVisionCamera::start() and CameraControlService::applyMindVisionConfig.
// These two sites previously carried duplicated apply code that drifted (the
// service applied only a 7-field subset). This header is SDK-free so it compiles
// on stub builds; the .cpp guards the SDK calls behind MIB_HAS_MINDVISION.
#pragma once

#include "backend/camera/mindvision/MindVisionConfig.h"

#include <string>
#include <vector>

namespace backend::camera::mindvision {

struct ApplyResult {
    bool ok{false};                     // false: fatal (file open, parse, SDK unavailable)
    Config config{};                    // parsed config; valid whenever parsing succeeded
    std::string error;                  // set when ok == false
    std::vector<std::string> warnings;  // parse clamps + per-field SDK failures (non-fatal)
};

// Applies EVERY Config field (resolution/ROI, exposure, trigger mode, gain, AE,
// gamma, contrast, sharpness, frame speed, mirror, strobe) to an open handle.
// Precondition: handle from CameraInit with CameraPlay NOT active — both call
// sites guarantee this (MindVisionCamera applies before CameraPlay; the control
// service opens a fresh handle with capture stopped). Individual SDK setter
// failures are recorded as warnings, not errors. On stub builds
// (MIB_HAS_MINDVISION=0) returns ok=false with an "SDK disabled" error.
ApplyResult applyConfigToCamera(int hCamera, const Config& config);

// Read file -> parseConfig -> applyConfigToCamera. The file/parse layer is
// always compiled (SDK-free), so file-open and parse errors are reported the
// same way on every platform.
ApplyResult applyJsonFileToCamera(int hCamera, const std::string& jsonPath);

} // namespace backend::camera::mindvision

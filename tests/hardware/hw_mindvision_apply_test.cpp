// hw_mindvision_apply_test  (LABEL: hardware) — runs only against a real
// MindVision camera on a Windows SDK build.
//
// Exercises the full shared apply sequence (applyJsonFileToCamera) against a
// live handle — the only place the real SDK setter calls run — and then guards
// checkDeviceHealth: repeated health checks while streaming must not consume
// frames from the image queue (the old probe-grab implementation silently
// discarded one frame per check, which in trigger mode could be a triggered
// target frame).
//
// Enable with MIB_TEST_MINDVISION_CONFIG=<path to a JSON config>; skips (77)
// otherwise, and always skips on stub builds. MIB_MINDVISION_CAMERA_INDEX
// selects the device (default 0).

#include "backend/camera/mindvision/MindVisionCamera.h"
#include "backend/camera/mindvision/MindVisionConfigApply.h"

#include "support/assert.h"
#include "support/hardware.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

int main()
{
#if !MIB_HAS_MINDVISION
    std::printf("SKIP: MindVision SDK disabled at build time\n");
    return mib::test::kSkipExitCode;
#else
    const std::string configPath = mib::test::requireDeviceEnv("MIB_TEST_MINDVISION_CONFIG");
    const int cameraIndex = mib::test::envInt("MIB_MINDVISION_CAMERA_INDEX", 0);

    // Full apply on a fresh non-streaming handle, exactly like the runtime
    // apply path (CameraControlService opens its own handle, applies, closes).
    camera::common::MindVisionCamera camera(cameraIndex, configPath);
    MIB_REQUIRE(camera.start(), "camera start (config applied before CameraPlay)");

    // Stream for a bit while hammering the health check; a healthy camera must
    // keep delivering frames and the check must never eat one.
    camera::common::Frame frame;
    MIB_REQUIRE(camera.grabFrame(frame), "first frame arrives");
    MIB_EXPECT(frame.width > 0 && frame.height > 0, "frame has dimensions");

    int grabbed = 1;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        MIB_REQUIRE(camera.checkDeviceHealth(), "health check while streaming");
        if (camera.grabFrame(frame)) ++grabbed;
    }
    std::printf("grabbed %d frames with interleaved health checks\n", grabbed);
    MIB_EXPECT(grabbed > 1, "frames keep flowing across health checks");

    camera.stop();
    if (mib::test::exitCode() == 0) std::printf("MindVision apply + health OK\n");
    return mib::test::exitCode();
#endif
}

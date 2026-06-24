// hw_camera_test  (LABEL: hardware) — runs only against a real camera.
//
// Requires a hardware build (EGrabber or MindVision SDK) and an attached
// camera. The operator selects the camera the same way the app does, via env:
//   MIB_CAMERA_MODE=mindvision|hardware  (+ MIB_MINDVISION_CAMERA_INDEX /
//   MIB_MINDVISION_CONFIG, or the hardware selection envs).
// Set MIB_TEST_CAMERA=1 to enable this test; it skips otherwise. It starts
// capture and asserts real frames flow within a few seconds.

#include "backend/app/AppBackend.h"
#include "backend/services/CaptureService.h"

#include "support/assert.h"
#include "support/hardware.h"
#include "support/tempdir.h"

#include <QCoreApplication>

#include <chrono>
#include <cstdio>
#include <thread>

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    mib::test::requireDeviceEnv("MIB_TEST_CAMERA");

    mib::test::TempDir td("mib_hw_camera");
    backend::AppBackend backend;
    MIB_REQUIRE(backend.initialize((td / "data").string()), "AppBackend initialize");
    MIB_REQUIRE(backend.isCameraConfigured(),
                "a camera is configured (set MIB_CAMERA_MODE + selection envs)");

    MIB_REQUIRE(backend.capture().start(), "capture start");

    bool gotFrames = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (backend.capture().stats().framesProcessed.load() > 0) { gotFrames = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const uint64_t n = backend.capture().stats().framesProcessed.load();
    std::printf("camera produced %llu frames\n", static_cast<unsigned long long>(n));
    MIB_EXPECT(gotFrames, "real camera delivered frames within 5s");

    backend.capture().stop();
    if (mib::test::exitCode() == 0) std::printf("camera hardware OK\n");
    return mib::test::exitCode();
}

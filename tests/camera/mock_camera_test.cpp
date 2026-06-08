#include "backend/camera/mock/MockCamera.h"

#include <filesystem>

int main()
{
    camera::mock::MockCameraOptions options;
    options.folder = std::filesystem::path(MIB_TEST_SOURCE_DIR) / "data" / "mock_frames";
    options.frameInterval = std::chrono::microseconds(1);
    options.loopFiles = true;

    camera::mock::MockCamera camera(options);
    if (!camera.start())
    {
        return 1;
    }

    camera::common::Frame frame;
    if (!camera.grabFrame(frame))
    {
        return 2;
    }
    if (frame.width == 0 || frame.height == 0 || frame.linePitch == 0 || frame.data.empty())
    {
        return 3;
    }

    camera::common::CameraStats stats;
    if (!camera.pollStats(stats))
    {
        return 4;
    }

    camera.stop();
    if (camera.isRunning())
    {
        return 5;
    }

    return 0;
}

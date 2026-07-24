#include "backend/app/AppBackend.h"
#include "backend/camera/mock/MockCamera.h"

#include <cstdlib>
#include <filesystem>

#include <spdlog/spdlog.h>

int main()
{
    backend::AppBackend app;
    if (!app.initialize("data/test_mindvision_selection_state"))
    {
        SPDLOG_ERROR("AppBackend initialization failed");
        return 1;
    }

    app.setMindVisionCameraSelection(2, "MindVision camera 2");
    if (!app.isMindVisionCameraSelected())
    {
        SPDLOG_ERROR("MindVision selection state was not recorded");
        return 2;
    }

    if (!app.isCameraConfigured())
    {
        SPDLOG_ERROR("MindVision selection did not mark the backend configured");
        return 3;
    }

    app.setHardwareCameraSelection(1, 4, "EGrabber camera 1/4");
    if (app.isMindVisionCameraSelected())
    {
        SPDLOG_ERROR("Hardware selection did not clear the MindVision selection state");
        return 4;
    }

    app.configureMockCamera(camera::mock::MockCameraOptions{});
    if (app.isMindVisionCameraSelected())
    {
        SPDLOG_ERROR("Mock selection did not clear the MindVision selection state");
        return 5;
    }

    // Runtime config apply must fail cleanly (error set, false returned) when
    // no MindVision camera is selected...
    std::string applyErr;
    if (app.applyMindVisionConfigFromFile("/nonexistent/mindvision.json", &applyErr))
    {
        SPDLOG_ERROR("applyMindVisionConfigFromFile succeeded with no MindVision selection");
        return 6;
    }
    if (applyErr.empty())
    {
        SPDLOG_ERROR("applyMindVisionConfigFromFile did not report an error without a selection");
        return 7;
    }

    // ...and with a selection but no usable SDK/config file (stub builds report
    // the SDK as disabled; SDK builds fail on the missing file/device).
    app.setMindVisionCameraSelection(0, "MindVision camera 0");
    applyErr.clear();
    if (app.applyMindVisionConfigFromFile("/nonexistent/mindvision.json", &applyErr))
    {
        SPDLOG_ERROR("applyMindVisionConfigFromFile succeeded against a nonexistent config");
        return 8;
    }
    if (applyErr.empty())
    {
        SPDLOG_ERROR("applyMindVisionConfigFromFile did not report an error for a failed apply");
        return 9;
    }

    SPDLOG_INFO("MindVision selection state test passed");
    return 0;
}

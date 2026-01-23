#pragma once

#include <memory>
#include <string>

namespace backend { class BackgroundCaptureNotifier; }

namespace backend::services
{
    class SqliteService;
    class Hdf5Service;
    class CaptureService;
    class ProcessingService;
    class PlaybackService;
    class CameraControlService;
    class AutofocusService;
}

namespace backend
{
    namespace playback
    {
        class FrameStore;
    }
}
namespace camera::mock
{
    struct MockCameraOptions;
}

namespace backend
{

    class AppBackend
    {
    public:
        AppBackend();
        ~AppBackend();

        bool initialize(const std::string &dataDir);

        services::SqliteService &sqlite();
        services::Hdf5Service &hdf5();
        services::CaptureService &capture();
        services::ProcessingService &processing();
        services::PlaybackService &playback();
        services::CameraControlService &cameraControl();
        services::AutofocusService &autofocus();
        
        // Get frame store for service lifecycle management
        std::shared_ptr<playback::FrameStore> getFrameStore() const { return frameStore_; }

        void configureMockCamera(const camera::mock::MockCameraOptions &options);

        // Select a specific hardware device (does not start capture)
        void setHardwareCameraSelection(int interfaceIndex, int deviceIndex, const std::string &label);

        // Apply a JS camera script to currently selected hardware device.
        // If capture is running, it will be stopped first. Capture remains stopped.
        bool applyCameraScriptFromFile(const std::string &path, std::string *errorOut = nullptr);

        // Issue GenICam DeviceReset to the selected hardware camera.
        // If capture is running, it will be stopped first. Capture remains stopped.
        bool resetSelectedHardwareCamera(std::string *errorOut = nullptr);

        // Check if a camera is configured (either hardware or mock)
        bool isCameraConfigured() const;

        // Get background capture notifier for Qt signal connections
        BackgroundCaptureNotifier* backgroundCaptureNotifier() const;

    private:
        std::unique_ptr<services::SqliteService> sqliteService_;
        std::unique_ptr<services::Hdf5Service> hdf5Service_;
        std::unique_ptr<services::CaptureService> captureService_;
        std::unique_ptr<services::ProcessingService> processingService_;
        std::unique_ptr<services::PlaybackService> playbackService_;
        std::unique_ptr<services::CameraControlService> cameraControlService_;
        std::unique_ptr<services::AutofocusService> autofocusService_;
        std::shared_ptr<playback::FrameStore> frameStore_;

        // Last selected hardware device (for script apply)
        int selectedIfIndex_{-1};
        int selectedDevIndex_{-1};
        std::string selectedLabel_;
        bool mockCameraConfigured_{false};
        
        // Background capture notifier for Qt signals
        std::unique_ptr<BackgroundCaptureNotifier> backgroundCaptureNotifier_;
    };

} // namespace backend

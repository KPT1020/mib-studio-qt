#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

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
    class TriggerService;
    class YoloService;
    class SyringePumpService;
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
        services::TriggerService &trigger();
        services::YoloService &yolo();
        services::SyringePumpService &syringePump();
        
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

        // Frame recording mode: record non-empty frames directly to HDF5 (images + metadata only, no contour processing)
        // Returns false if recording cannot start (e.g., capture not running, file error)
        bool startFrameRecording(const std::string& hdf5FilePath);
        void stopFrameRecording();
        bool isFrameRecording() const;
        uint64_t frameRecordingCount() const;     // Frames written so far
        uint64_t frameRecordingFiltered() const;   // Empty frames skipped

        // Raw config JSON storage (set by config watcher, read at experiment save)
        void setLastConfigJson(const std::string& json);
        std::string getLastConfigJson() const;

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
        std::unique_ptr<services::TriggerService> triggerService_;
        std::unique_ptr<services::YoloService> yoloService_;
        std::unique_ptr<services::SyringePumpService> syringePumpService_;
        std::shared_ptr<playback::FrameStore> frameStore_;

        // Last selected hardware device (for script apply)
        int selectedIfIndex_{-1};
        int selectedDevIndex_{-1};
        std::string selectedLabel_;
        bool mockCameraConfigured_{false};

        // Frame recording state
        std::unique_ptr<std::thread> frameRecordingThread_;
        std::atomic<bool> frameRecordingRunning_{false};
        std::atomic<uint64_t> frameRecordingWritten_{0};
        std::atomic<uint64_t> frameRecordingFiltered_{0};
        std::string frameRecordingPath_;

        // Background capture notifier for Qt signals
        std::unique_ptr<BackgroundCaptureNotifier> backgroundCaptureNotifier_;

        // Raw config JSON for HDF5 metadata persistence
        mutable std::mutex configJsonMutex_;
        std::string lastConfigJson_;
    };

} // namespace backend

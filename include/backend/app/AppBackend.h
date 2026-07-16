#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "backend/app/BackgroundFrame.h"
#include "backend/processing/EModulusLutCatalog.h" // HttpGetFn seam (ADR 0002)

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

        // Inject the HTTP GET used to fetch the E-modulus LUT manifest/blob
        // (ADR 0002); the shell supplies it so the backend links no Qt
        // networking. Without a fetcher, remote LUT fetch is skipped and the
        // cached/bundled LUT is used. Call before initialize().
        void setLutHttpFetcher(HttpGetFn fetcher) { lutHttpGet_ = std::move(fetcher); }
        // Base directory for the LUT cache — the shell passes the platform
        // app-data dir so the on-disk cache location is unchanged. Call before
        // initialize(). (The env override MIB_STUDIO_EMODULUS_LUT_CACHE_DIR
        // still takes precedence.)
        void setLutAppDataDir(std::string dir) { lutAppDataDir_ = std::move(dir); }

        // Stop every service-owned thread in dependency order (capture →
        // trigger → recording → realtime/processing). Idempotent; called by
        // the destructor so teardown never depends on GUI close handling.
        void shutdown();

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

        // Select a MindVision camera by enumeration index (does not start capture)
        void setMindVisionCameraSelection(int cameraIndex, const std::string &label);

        // Apply a JS camera script to currently selected hardware device.
        // If capture is running, it will be stopped first. Capture remains stopped.
        bool applyCameraScriptFromFile(const std::string &path, std::string *errorOut = nullptr);

        // Apply a JSON config file to the currently selected MindVision camera.
        // If capture is running, it will be stopped first. Capture remains stopped.
        bool applyMindVisionConfigFromFile(const std::string &path, std::string *errorOut = nullptr);

        // Returns true if a MindVision camera is currently selected.
        bool isMindVisionCameraSelected() const;

        // Issue GenICam DeviceReset to the selected hardware camera.
        // If capture is running, it will be stopped first. Capture remains stopped.
        bool resetSelectedHardwareCamera(std::string *errorOut = nullptr);

        // Check if a camera is configured (either hardware or mock)
        bool isCameraConfigured() const;

        // Authoritative selected-device snapshot (BE-2, #272): which source is
        // selected, its identity/labels/indices, applied config/script paths,
        // and the mock parameters. Values survive capture start/stop.
        struct CameraSelectionSnapshot
        {
            enum class Mode
            {
                None,
                Mock,
                Hardware,
                MindVision,
            };
            Mode mode{Mode::None};
            int interfaceIndex{-1};
            int deviceIndex{-1};
            std::string label;
            int mindVisionIndex{-1};
            std::string mindVisionConfigPath;
            std::string cameraScriptPath;
            std::string mockFrameDir;
            int mockIntervalMs{0};
            bool mockLoop{true};
            bool configured{false};
        };
        CameraSelectionSnapshot cameraSelection() const;

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

        using BackgroundCaptureCallback = std::function<void(const BackgroundCaptureEvent& event)>;
        void setBackgroundCaptureCallback(BackgroundCaptureCallback callback);

        // Fatal save-error sink: invoked (possibly on a writer thread) when an
        // experiment flush or recording write fails, or the write queue
        // overflows. The active operation is stopped; the UI should surface the
        // message. Funnels both recording and experiment flush failures.
        using FatalSaveErrorCallback = std::function<void(const std::string&)>;
        void setFatalSaveErrorCallback(FatalSaveErrorCallback callback);

    private:
        void reportFatalSaveError(const std::string& msg);

        FatalSaveErrorCallback fatalSaveErrorCb_;

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

        // Shell-injected LUT fetch config (ADR 0002).
        HttpGetFn lutHttpGet_;
        std::string lutAppDataDir_;

        // Last selected hardware device (for script apply)
        int selectedIfIndex_{-1};
        int selectedDevIndex_{-1};
        std::string selectedLabel_;
        int selectedMvCameraIndex_{-1};
        std::string lastMindVisionConfigPath_;
        bool mockCameraConfigured_{false};
        // Selection-snapshot extras (BE-2): last applied camera script and the
        // active mock parameters.
        std::string lastCameraScriptPath_;
        std::string mockFrameDir_;
        int mockIntervalMs_{0};
        bool mockLoop_{true};

        // Frame recording state
        std::unique_ptr<std::thread> frameRecordingThread_;
        std::atomic<bool> frameRecordingRunning_{false};
        std::atomic<uint64_t> frameRecordingWritten_{0};
        std::atomic<uint64_t> frameRecordingFiltered_{0};
        std::string frameRecordingPath_;

        mutable std::mutex backgroundCaptureCallbackMutex_;
        BackgroundCaptureCallback backgroundCaptureCallback_;

        // Raw config JSON for HDF5 metadata persistence
        mutable std::mutex configJsonMutex_;
        std::string lastConfigJson_;
    };

} // namespace backend

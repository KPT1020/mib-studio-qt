#include "backend/AppBackend.h"

#include "backend/services/Logger.h"
#include "backend/services/SqliteService.h"
#include "backend/services/Hdf5Service.h"
#include "backend/services/CaptureService.h"
#include "backend/services/ProcessingService.h"
#include "backend/services/PlaybackService.h"
#include "backend/playback/FrameStore.h"
#include "camera/common/EGrabberCamera.h"
#include "camera/mock/MockCamera.h"
#include "backend/services/CameraControlService.h"
#include "backend/services/AutofocusService.h"
#include "backend/services/TriggerService.h"
#include "backend/services/YoloService.h"
#include "backend/BackgroundCaptureNotifier.h"
#include <QImage>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <cctype>
#include <string>
#include <spdlog/spdlog.h>
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace backend
{
    namespace
    {
        // Get a user-writable log path, falling back to dataDir if needed
        std::string getLogPath(const std::string &dataDir)
        {
            std::filesystem::path dataPath(dataDir);
            std::string logPath;

#ifdef _WIN32
            // Check if dataDir is in Program Files (requires admin to write)
            std::string dataDirLower = dataDir;
            std::transform(dataDirLower.begin(), dataDirLower.end(), dataDirLower.begin(),
                           [](unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });

            // Check if path contains "program files" (common install location)
            if (dataDirLower.find("program files") != std::string::npos ||
                dataDirLower.find("program files (x86)") != std::string::npos)
            {
                // Use user-writable location instead
                char appDataPath[MAX_PATH];
                if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appDataPath)))
                {
                    std::filesystem::path userLogDir = std::filesystem::path(appDataPath) / "MIB_Studio_Qt" / "logs";
                    std::filesystem::create_directories(userLogDir);
                    logPath = (userLogDir / "app.log").string();
                    return logPath;
                }
            }
#endif
            // Default: use dataDir/logs/app.log
            std::filesystem::create_directories(dataPath / "logs");
            logPath = (dataPath / "logs" / "app.log").string();
            return logPath;
        }
    }

    AppBackend::AppBackend() {
        backgroundCaptureNotifier_ = std::make_unique<BackgroundCaptureNotifier>();
    }
    AppBackend::~AppBackend() = default;

    bool AppBackend::initialize(const std::string &dataDir)
    {
        std::filesystem::create_directories(dataDir);

        // Use user-writable location for logs if dataDir is in Program Files
        std::string logPath = getLogPath(dataDir);
        backend::services::Logger::init(logPath);

        sqliteService_ = std::make_unique<services::SqliteService>();
        hdf5Service_ = std::make_unique<services::Hdf5Service>();
        captureService_ = std::make_unique<services::CaptureService>();
        processingService_ = std::make_unique<services::ProcessingService>();
        playbackService_ = std::make_unique<services::PlaybackService>();
        cameraControlService_ = std::make_unique<services::CameraControlService>();
        autofocusService_ = std::make_unique<services::AutofocusService>();
        triggerService_ = std::make_unique<services::TriggerService>();
        yoloService_ = std::make_unique<services::YoloService>();
        frameStore_ = std::make_shared<playback::FrameStore>(5000);

        sqliteService_->initialize((std::filesystem::path(dataDir) / "app.sqlite3").string());
        hdf5Service_->initialize(dataDir);

        // Initialize YOLO service - resolve model path relative to data directory
        // dataDir is typically {exeDir}/data, so we go up one level to get exeDir
        std::filesystem::path dataPath(dataDir);
        std::filesystem::path exeDir = dataPath.parent_path();
        std::filesystem::path modelPath = exeDir / "resources" / "models" / "yolo11n-seg.onnx";
        if (!yoloService_->initialize(modelPath.string())) {
            SPDLOG_WARN("YOLO model not loaded - segmentation features will not be available");
        }

        processingService_->start();
        // Note: startRealtime() is now called when Experiment tab becomes active, not during initialization

        // Wire autofocus service to receive ring ratios from processing service
        processingService_->setRingRatioCallback([this](double ringRatio, int64_t timestampNs)
                                                 {
            if (autofocusService_) {
                autofocusService_->onRingRatio(ringRatio, timestampNs);
            } });

        // Wire target group trigger: processing -> trigger service
        processingService_->setTargetGroupCallback([this](bool isTargetGroup) {
            if (triggerService_) {
                triggerService_->onTargetGroupResult(isTargetGroup);
            }
        });

        // Wire camera lifecycle to trigger service
        captureService_->setCameraReadyCallback([this](camera::common::ICamera* cam) {
            if (triggerService_) {
                triggerService_->setCamera(cam);
                if (cam) {
                    triggerService_->start();
                } else {
                    triggerService_->stop();
                }
            }
        });

        // Wire background capture callback to emit Qt signal
        processingService_->setBackgroundCaptureCallback([this](const cv::Mat& bg, uint64_t frameIndex) {
            if (backgroundCaptureNotifier_) {
                // Convert cv::Mat to QImage
                QImage qimg(bg.data, bg.cols, bg.rows, static_cast<int>(bg.step), QImage::Format_Grayscale8);
                QImage qimgCopy = qimg.copy(); // Ensure we own the data
                // Use QTimer::singleShot to ensure we're in the Qt event loop thread
                QTimer::singleShot(0, backgroundCaptureNotifier_.get(), [this, qimgCopy, frameIndex]() {
                    emit backgroundCaptureNotifier_->backgroundAutoCaptured(qimgCopy, frameIndex);
                });
            }
            SPDLOG_INFO("Background auto-captured at frame {}", frameIndex);
        });

        // Wire capture -> frame store for playback/display
        captureService_->setFrameStore(frameStore_);

        // Configure camera source (hardware or mock) before we start streaming.
        auto toLower = [](std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });
            return value;
        };

        std::string cameraMode = "hardware";
        if (const char *envMode = std::getenv("MIB_CAMERA_MODE"))
        {
            cameraMode = toLower(envMode);
        }

        if (cameraMode == "mock")
        {
            camera::mock::MockCameraOptions options;
            if (const char *envDir = std::getenv("MIB_MOCK_CAMERA_DIR"))
            {
                options.folder = std::filesystem::path(envDir);
            }
            else
            {
                options.folder = std::filesystem::path(dataDir) / "mock_frames";
            }
            if (const char *envInterval = std::getenv("MIB_MOCK_CAMERA_INTERVAL_MS"))
            {
                try
                {
                    const int ms = std::stoi(envInterval);
                    if (ms > 0)
                    {
                        options.frameInterval = std::chrono::milliseconds(ms);
                    }
                }
                catch (const std::exception &)
                {
                    SPDLOG_WARN("Invalid MIB_MOCK_CAMERA_INTERVAL_MS value: {}", envInterval);
                }
            }
            if (const char *envLoop = std::getenv("MIB_MOCK_CAMERA_LOOP"))
            {
                const std::string loopValue = toLower(envLoop);
                options.loopFiles = (loopValue != "false" && loopValue != "0" && loopValue != "no");
            }

            const auto intervalUs = options.frameInterval.count();
            const double configuredFps = intervalUs > 0 ? 1'000'000.0 / static_cast<double>(intervalUs) : 0.0;
            SPDLOG_INFO("AppBackend: configuring MockCamera (folder={}, interval={} us, ~{:.1f} fps, loop={})",
                        options.folder.string(),
                        intervalUs,
                        configuredFps,
                        options.loopFiles);

            captureService_->setCameraFactory([options]() mutable
                                              { return std::make_unique<camera::mock::MockCamera>(options); });
            mockCameraConfigured_ = true;
        }
        else
        {
            SPDLOG_INFO("AppBackend: configuring hardware EGrabber camera");
            captureService_->setCameraFactory([]()
                                              { return std::make_unique<camera::common::EGrabberCamera>(); });
            mockCameraConfigured_ = false;
        }
        playbackService_->setFrameStore(frameStore_);

        // No per-frame logging; rely on periodic capture stats
        captureService_->setFrameCallback(nullptr);

        SPDLOG_INFO("Backend initialized.");
        return true;
    }

    services::SqliteService &AppBackend::sqlite() { return *sqliteService_; }
    services::Hdf5Service &AppBackend::hdf5() { return *hdf5Service_; }
    services::CaptureService &AppBackend::capture() { return *captureService_; }
    services::ProcessingService &AppBackend::processing() { return *processingService_; }
    services::PlaybackService &AppBackend::playback() { return *playbackService_; }
    services::CameraControlService &AppBackend::cameraControl() { return *cameraControlService_; }
    services::AutofocusService &AppBackend::autofocus() { return *autofocusService_; }
    services::TriggerService &AppBackend::trigger() { return *triggerService_; }
    services::YoloService &AppBackend::yolo() { return *yoloService_; }

    void AppBackend::configureMockCamera(const camera::mock::MockCameraOptions &options)
    {
        if (!captureService_)
            return;
        captureService_->setCameraFactory([options]() mutable
                                          { return std::make_unique<camera::mock::MockCamera>(options); });
        selectedIfIndex_ = -1;
        selectedDevIndex_ = -1;
        selectedLabel_.clear();
        mockCameraConfigured_ = true;
    }

    void AppBackend::setHardwareCameraSelection(int interfaceIndex, int deviceIndex, const std::string &label)
    {
        if (!captureService_)
            return;
        selectedIfIndex_ = interfaceIndex;
        selectedDevIndex_ = deviceIndex;
        selectedLabel_ = label;
        mockCameraConfigured_ = false;

        captureService_->setCameraFactory([interfaceIndex, deviceIndex]()
                                          { return std::make_unique<camera::common::EGrabberCamera>(interfaceIndex, deviceIndex); });
        SPDLOG_INFO("Hardware camera selected: {} (if={}, dev={})",
                    label, interfaceIndex, deviceIndex);
    }

    bool AppBackend::applyCameraScriptFromFile(const std::string &path, std::string *errorOut)
    {
        if (selectedIfIndex_ < 0 || selectedDevIndex_ < 0)
        {
            if (errorOut)
                *errorOut = "No hardware camera selected";
            return false;
        }
        // Ensure capture thread is stopped
        if (captureService_ && captureService_->isRunning())
        {
            SPDLOG_INFO("Stopping capture before applying camera script");
            captureService_->stop();
        }
        SPDLOG_INFO("Applying camera script to {} from {}", selectedLabel_, path);
        return cameraControlService_->applyScriptToDevice(selectedIfIndex_, selectedDevIndex_, path, errorOut);
    }

    bool AppBackend::resetSelectedHardwareCamera(std::string *errorOut)
    {
        if (selectedIfIndex_ < 0 || selectedDevIndex_ < 0)
        {
            if (errorOut)
                *errorOut = "No hardware camera selected";
            return false;
        }
        // Ensure capture thread is stopped
        if (captureService_ && captureService_->isRunning())
        {
            SPDLOG_INFO("Stopping capture before camera reset");
            captureService_->stop();
        }
        SPDLOG_INFO("Resetting camera {}", selectedLabel_);
        return cameraControlService_->deviceReset(selectedIfIndex_, selectedDevIndex_, errorOut);
    }

    bool AppBackend::isCameraConfigured() const
    {
        // Camera is configured if hardware camera is selected OR mock camera is configured
        return (selectedIfIndex_ >= 0 && selectedDevIndex_ >= 0) || mockCameraConfigured_;
    }

    BackgroundCaptureNotifier* AppBackend::backgroundCaptureNotifier() const {
        return backgroundCaptureNotifier_.get();
    }

} // namespace backend

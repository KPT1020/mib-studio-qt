#include "backend/AppBackend.h"

#include "backend/services/Logger.h"
#include "backend/services/CrashReporter.h"
#include "backend/diagnostics/CrashStateMirror.h"
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
#include "backend/services/SyringePumpService.h"
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

#ifndef MIB_HAS_EGRABBER
#define MIB_HAS_EGRABBER 0
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
    AppBackend::~AppBackend() {
        stopFrameRecording();
    }

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
        syringePumpService_ = std::make_unique<services::SyringePumpService>();
        frameStore_ = std::make_shared<playback::FrameStore>(5000);

        bool bootSqlite = true;
        bool bootHdf5 = true;
        bool bootProcessing = true;
        bool bootYolo = true;
        bool bootAutofocus = true;
        bool bootTrigger = true;
        bool bootCapture = true;
        bool bootPlayback = true;
        if (const char *rawDisabledServices = std::getenv("MIB_DISABLED_SERVICES"))
        {
            std::string disabled(rawDisabledServices);
            auto normalize = [](std::string token)
            {
                const auto first = token.find_first_not_of(" 	
");
                if (first == std::string::npos)
                {
                    return std::string{};
                }
                const auto last = token.find_last_not_of(" 	
");
                token = token.substr(first, last - first + 1);
                std::transform(token.begin(), token.end(), token.begin(), [](unsigned char c)
                               { return static_cast<char>(std::tolower(c)); });
                std::replace(token.begin(), token.end(), '-', '_');
                return token;
            };
            size_t cursor = 0;
            while (cursor <= disabled.size())
            {
                const size_t next = disabled.find(',', cursor);
                const std::string token = normalize(disabled.substr(cursor, next == std::string::npos ? std::string::npos : next - cursor));
                if (token == "all")
                {
                    bootSqlite = false;
                    bootHdf5 = false;
                    bootProcessing = false;
                    bootYolo = false;
                    bootAutofocus = false;
                    bootTrigger = false;
                    bootCapture = false;
                    bootPlayback = false;
                }
                else if (token == "sqlite")
                {
                    bootSqlite = false;
                }
                else if (token == "hdf5")
                {
                    bootHdf5 = false;
                }
                else if (token == "processing")
                {
                    bootProcessing = false;
                }
                else if (token == "yolo")
                {
                    bootYolo = false;
                }
                else if (token == "autofocus")
                {
                    bootAutofocus = false;
                }
                else if (token == "trigger")
                {
                    bootTrigger = false;
                }
                else if (token == "capture" || token == "camera")
                {
                    bootCapture = false;
                }
                else if (token == "playback")
                {
                    bootPlayback = false;
                }
                else if (token == "auto_update")
                {
                    // Handled by frontend startup path.
                }
                else if (!token.empty())
                {
                    SPDLOG_WARN("Unknown token '{}' in MIB_DISABLED_SERVICES", token);
                }
                if (next == std::string::npos)
                {
                    break;
                }
                cursor = next + 1;
            }
        }

        SPDLOG_INFO("AppBackend boot toggles: sqlite={}, hdf5={}, processing={}, yolo={}, autofocus={}, trigger={}, capture={}, playback={}",
                    bootSqlite, bootHdf5, bootProcessing, bootYolo, bootAutofocus, bootTrigger, bootCapture, bootPlayback);

        if (bootSqlite)
        {
            sqliteService_->initialize((std::filesystem::path(dataDir) / "app.sqlite3").string());
        }
        else
        {
            SPDLOG_WARN("AppBackend: sqlite bootstrap disabled by MIB_DISABLED_SERVICES");
        }

        if (bootHdf5)
        {
            hdf5Service_->initialize(dataDir);
        }
        else
        {
            SPDLOG_WARN("AppBackend: hdf5 bootstrap disabled by MIB_DISABLED_SERVICES");
        }

        // Initialize YOLO service - resolve model path relative to data directory
        // dataDir is typically {exeDir}/data, so we go up one level to get exeDir
        std::filesystem::path dataPath(dataDir);
        std::filesystem::path exeDir = dataPath.parent_path();
        std::filesystem::path modelPath = exeDir / "resources" / "models" / "yolo11n-seg.onnx";
        if (bootYolo)
        {
            if (!yoloService_->initialize(modelPath.string()))
            {
                SPDLOG_WARN("YOLO model not loaded - segmentation features will not be available");
            }
        }
        else
        {
            SPDLOG_WARN("AppBackend: yolo bootstrap disabled by MIB_DISABLED_SERVICES");
        }

        // Load Young's modulus LUT for emodulus gating
        if (bootProcessing)
        {
            std::filesystem::path lutPath = exeDir / "resources" / "isoelastic_curve" / "scaled_isoelastic_data_LUT_6.16-4.24.txt";
            if (!processingService_->loadEModulusLut(lutPath.string()))
            {
                SPDLOG_WARN("Young's modulus LUT not loaded - emodulus gating will not be available");
            }
            processingService_->start();
        }
        else
        {
            SPDLOG_WARN("AppBackend: processing bootstrap disabled by MIB_DISABLED_SERVICES");
        }
        // Note: startRealtime() is now called when Experiment tab becomes active, not during initialization

        // Wire autofocus service to receive ring ratios from processing service
        if (bootProcessing && bootAutofocus)
        {
            processingService_->setRingRatioCallback([this](double ringRatio, int64_t timestampNs)
                                                     {
                if (autofocusService_) {
                    autofocusService_->onRingRatio(ringRatio, timestampNs);
                } });
        }
        else
        {
            processingService_->setRingRatioCallback({});
            if (!bootAutofocus)
            {
                SPDLOG_WARN("AppBackend: autofocus ring-ratio callback disabled by MIB_DISABLED_SERVICES");
            }
        }

        // Wire target group trigger: processing -> trigger service
        if (bootProcessing && bootTrigger)
        {
            processingService_->setTargetGroupCallback([this](bool isTargetGroup) {
                if (triggerService_) {
                    triggerService_->onTargetGroupResult(isTargetGroup);
                }
            });
        }
        else
        {
            processingService_->setTargetGroupCallback({});
            if (!bootTrigger)
            {
                SPDLOG_WARN("AppBackend: trigger callback wiring disabled by MIB_DISABLED_SERVICES");
            }
        }

        // Wire camera lifecycle to trigger service
        if (bootCapture && bootTrigger)
        {
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
        }
        else
        {
            captureService_->setCameraReadyCallback({});
            if (triggerService_)
            {
                triggerService_->setCamera(nullptr);
                triggerService_->stop();
            }
        }

        // Wire background capture callback to emit Qt signal
        if (bootProcessing)
        {
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
        }
        else
        {
            processingService_->setBackgroundCaptureCallback({});
        }

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

        if (bootCapture)
        {
            // Wire capture -> frame store for playback/display
            captureService_->setFrameStore(frameStore_);

            // Configure camera source (hardware or mock) before we start streaming.
#if !MIB_HAS_EGRABBER
            if (cameraMode != "mock")
            {
                SPDLOG_WARN("AppBackend: forcing mock camera mode because EGrabber SDK is unavailable on this platform");
                cameraMode = "mock";
            }
#endif

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
#if MIB_HAS_EGRABBER
                SPDLOG_INFO("AppBackend: configuring hardware EGrabber camera");
                captureService_->setCameraFactory([]()
                                                  { return std::make_unique<camera::common::EGrabberCamera>(); });
                mockCameraConfigured_ = false;
#else
                SPDLOG_WARN("AppBackend: hardware mode requested but EGrabber SDK is unavailable; keeping mock camera");
                camera::mock::MockCameraOptions options;
                options.folder = std::filesystem::path(dataDir) / "mock_frames";
                captureService_->setCameraFactory([options]() mutable
                                                  { return std::make_unique<camera::mock::MockCamera>(options); });
                mockCameraConfigured_ = true;
#endif
            }

            // No per-frame logging; rely on periodic capture stats
            captureService_->setFrameCallback(nullptr);
        }
        else
        {
            SPDLOG_WARN("AppBackend: capture bootstrap disabled by MIB_DISABLED_SERVICES");
            cameraMode = "disabled";
            selectedIfIndex_ = -1;
            selectedDevIndex_ = -1;
            selectedLabel_.clear();
            mockCameraConfigured_ = false;
        }

        if (bootPlayback)
        {
            playbackService_->setFrameStore(frameStore_);
        }
        else
        {
            SPDLOG_WARN("AppBackend: playback bootstrap disabled by MIB_DISABLED_SERVICES");
        }

        // Wire up the crash-state mirror so post-crash dumps include the
        // current camera / data-dir context. (CrashReporter::init() runs
        // earlier in main(), before AppBackend exists.)
        {
            auto& mirror = backend::diagnostics::CrashStateMirror::instance();
            mirror.setDataDir(dataDir);
            mirror.app.mockCamera.store(mockCameraConfigured_);
            mirror.app.selectedInterface.store(selectedIfIndex_);
            mirror.app.selectedDevice.store(selectedDevIndex_);
            mirror.setCameraLabel(selectedLabel_);
            mirror.frameStore.capacity.store(5000);
            backend::services::CrashReporter::setTag("camera_mode", cameraMode);
            backend::services::CrashReporter::setTag("data_dir", dataDir);
            backend::services::CrashReporter::breadcrumb("lifecycle",
                "AppBackend initialized");
        }

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
    services::SyringePumpService &AppBackend::syringePump() { return *syringePumpService_; }

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

#if !MIB_HAS_EGRABBER
        SPDLOG_WARN("Hardware camera selection ignored: EGrabber SDK is unavailable on this platform");
        camera::mock::MockCameraOptions options;
        options.folder = std::filesystem::path("data") / "mock_frames";
        captureService_->setCameraFactory([options]() mutable
                                          { return std::make_unique<camera::mock::MockCamera>(options); });
        selectedIfIndex_ = -1;
        selectedDevIndex_ = -1;
        selectedLabel_.clear();
        mockCameraConfigured_ = true;
        {
            auto& mirror = backend::diagnostics::CrashStateMirror::instance();
            mirror.app.selectedInterface.store(-1);
            mirror.app.selectedDevice.store(-1);
            mirror.app.mockCamera.store(true);
            mirror.setCameraLabel("");
        }
        return;
#else
        {
            auto& mirror = backend::diagnostics::CrashStateMirror::instance();
            mirror.app.selectedInterface.store(interfaceIndex);
            mirror.app.selectedDevice.store(deviceIndex);
            mirror.app.mockCamera.store(false);
            mirror.setCameraLabel(label);
        }
#endif

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

    bool AppBackend::startFrameRecording(const std::string& hdf5FilePath) {
        if (frameRecordingRunning_.load()) {
            SPDLOG_WARN("Frame recording already in progress");
            return false;
        }
        if (!captureService_ || !captureService_->isRunning()) {
            SPDLOG_ERROR("Cannot start frame recording: camera not running");
            return false;
        }

        // Open HDF5 file for recording
        auto& hdf5 = *hdf5Service_;
        if (hdf5.isFileOpen()) {
            SPDLOG_WARN("HDF5 file already open, closing before recording");
            hdf5.closeFile();
        }

        std::string path = hdf5FilePath;
        if (path.size() < 3 || (path.substr(path.size() - 3) != ".h5" &&
            (path.size() < 5 || path.substr(path.size() - 5) != ".hdf5"))) {
            path += ".h5";
        }

        if (!hdf5.openFile(path)) {
            SPDLOG_ERROR("Failed to open HDF5 file for recording: {}", path);
            return false;
        }
        if (!hdf5.initializeRecordingDatasets()) {
            hdf5.closeFile();
            return false;
        }

        frameRecordingPath_ = path;
        frameRecordingWritten_.store(0);
        frameRecordingFiltered_.store(0);
        frameRecordingRunning_.store(true);
        {
            auto& m = backend::diagnostics::CrashStateMirror::instance().recorder;
            m.recording.store(true);
            m.framesWritten.store(0);
            m.framesFiltered.store(0);
        }
        backend::services::CrashReporter::breadcrumb("recording",
            "frame recording started", path);

        // Launch recording thread
        frameRecordingThread_ = std::make_unique<std::thread>([this]() {
            SPDLOG_INFO("Frame recording thread started");

            const uint64_t startTimeNs = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());

            uint64_t lastProcessedIdx = 0;
            bool firstFrame = true;
            constexpr size_t FLUSH_BATCH = 50; // Flush every N frames

            std::vector<cv::Mat> batchImages;
            std::vector<services::Hdf5Service::RecordingFrameMeta> batchMeta;
            batchImages.reserve(FLUSH_BATCH);
            batchMeta.reserve(FLUSH_BATCH);

            while (frameRecordingRunning_.load()) {
                // Get latest available index from FrameStore
                const uint64_t totalWritten = frameStore_->totalWritten();
                if (totalWritten == 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    continue;
                }

                const uint64_t latestIdx = totalWritten - 1;
                const uint64_t startIdx = firstFrame ? latestIdx : lastProcessedIdx + 1;
                firstFrame = false;

                if (startIdx > latestIdx) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }

                // Process new frames
                for (uint64_t idx = startIdx; idx <= latestIdx && frameRecordingRunning_.load(); ++idx) {
                    playback::Frame f{};
                    if (!frameStore_->getByWriteIndex(idx, f)) {
                        continue;
                    }
                    if (f.width == 0 || f.height == 0 || f.data.empty()) {
                        continue;
                    }

                    // Check if empty using processing service config
                    auto config = processingService_->getProcessingConfig();
                    auto roi = processingService_->getRealtimeRoi();
                    auto bg = processingService_->getRealtimeBackgroundGray();

                    if (services::ProcessingService::isFrameEmpty(f, config, roi, bg)) {
                        frameRecordingFiltered_.fetch_add(1, std::memory_order_relaxed);
                        lastProcessedIdx = idx;
                        continue;
                    }

                    // Convert to cv::Mat
                    const int w = static_cast<int>(f.width);
                    const int h = static_cast<int>(f.height);
                    const size_t step = (f.linePitch == 0 ? static_cast<size_t>(f.width) : f.linePitch);
                    cv::Mat view(h, w, CV_8UC1, f.data.data(), step);
                    batchImages.push_back(view.clone());

                    services::Hdf5Service::RecordingFrameMeta meta;
                    meta.index = idx;
                    meta.timestampNs = f.timestamp;
                    meta.width = f.width;
                    meta.height = f.height;
                    batchMeta.push_back(meta);

                    lastProcessedIdx = idx;

                    // Flush batch when full
                    if (batchImages.size() >= FLUSH_BATCH) {
                        if (!hdf5Service_->appendRecordingFrames(batchImages, batchMeta)) {
                            SPDLOG_ERROR("Frame recording: failed to flush batch");
                        }
                        frameRecordingWritten_.fetch_add(batchImages.size(), std::memory_order_relaxed);
                        batchImages.clear();
                        batchMeta.clear();
                    }
                }
            }

            // Flush remaining frames
            if (!batchImages.empty()) {
                if (hdf5Service_->appendRecordingFrames(batchImages, batchMeta)) {
                    frameRecordingWritten_.fetch_add(batchImages.size(), std::memory_order_relaxed);
                }
            }

            // Write recording info
            const uint64_t endTimeNs = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());

            hdf5Service_->writeRecordingInfo(startTimeNs, endTimeNs,
                                             frameRecordingWritten_.load(),
                                             frameRecordingFiltered_.load());
            hdf5Service_->closeFile();

            SPDLOG_INFO("Frame recording stopped: {} frames recorded, {} empty filtered, file: {}",
                        frameRecordingWritten_.load(), frameRecordingFiltered_.load(), frameRecordingPath_);
        });

        SPDLOG_INFO("Frame recording started: {}", path);
        return true;
    }

    void AppBackend::stopFrameRecording() {
        if (!frameRecordingRunning_.load()) return;

        frameRecordingRunning_.store(false);
        if (frameRecordingThread_ && frameRecordingThread_->joinable()) {
            frameRecordingThread_->join();
        }
        frameRecordingThread_.reset();
        {
            auto& m = backend::diagnostics::CrashStateMirror::instance().recorder;
            m.recording.store(false);
            m.framesWritten.store(frameRecordingWritten_.load());
            m.framesFiltered.store(frameRecordingFiltered_.load());
        }
        backend::services::CrashReporter::breadcrumb("recording",
            "frame recording stopped");
    }

    bool AppBackend::isFrameRecording() const {
        return frameRecordingRunning_.load();
    }

    uint64_t AppBackend::frameRecordingCount() const {
        return frameRecordingWritten_.load();
    }

    uint64_t AppBackend::frameRecordingFiltered() const {
        return frameRecordingFiltered_.load();
    }

    BackgroundCaptureNotifier* AppBackend::backgroundCaptureNotifier() const {
        return backgroundCaptureNotifier_.get();
    }

    void AppBackend::setLastConfigJson(const std::string& json) {
        std::lock_guard<std::mutex> lk(configJsonMutex_);
        lastConfigJson_ = json;
    }

    std::string AppBackend::getLastConfigJson() const {
        std::lock_guard<std::mutex> lk(configJsonMutex_);
        return lastConfigJson_;
    }

} // namespace backend

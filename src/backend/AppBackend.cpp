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

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <cctype>
#include <string>
#include <spdlog/spdlog.h>

namespace backend
{

    AppBackend::AppBackend() = default;
    AppBackend::~AppBackend() = default;

    bool AppBackend::initialize(const std::string &dataDir)
    {
        std::filesystem::create_directories(dataDir);

        backend::services::Logger::init((std::filesystem::path(dataDir) / "logs" / "app.log").string());

        sqliteService_ = std::make_unique<services::SqliteService>();
        hdf5Service_ = std::make_unique<services::Hdf5Service>();
        captureService_ = std::make_unique<services::CaptureService>();
        processingService_ = std::make_unique<services::ProcessingService>();
        playbackService_ = std::make_unique<services::PlaybackService>();
        cameraControlService_ = std::make_unique<services::CameraControlService>();
        autofocusService_ = std::make_unique<services::AutofocusService>();
        frameStore_ = std::make_shared<playback::FrameStore>(512);

        sqliteService_->initialize((std::filesystem::path(dataDir) / "app.sqlite3").string());
        hdf5Service_->initialize(dataDir);

        processingService_->start();
        processingService_->startRealtime(frameStore_);

        // Wire autofocus service to receive ring ratios from processing service
        processingService_->setRingRatioCallback([this](double ringRatio, int64_t timestampNs) {
            if (autofocusService_) {
                autofocusService_->onRingRatio(ringRatio, timestampNs);
            }
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
        }
        else
        {
            SPDLOG_INFO("AppBackend: configuring hardware EGrabber camera");
            captureService_->setCameraFactory([]()
                                              { return std::make_unique<camera::common::EGrabberCamera>(); });
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

void AppBackend::configureMockCamera(const camera::mock::MockCameraOptions& options) {
    if (!captureService_) return;
    captureService_->setCameraFactory([options]() mutable {
        return std::make_unique<camera::mock::MockCamera>(options);
    });
    selectedIfIndex_ = -1;
    selectedDevIndex_ = -1;
    selectedLabel_.clear();
}

void AppBackend::setHardwareCameraSelection(int interfaceIndex, int deviceIndex, const std::string& label) {
    if (!captureService_) return;
    selectedIfIndex_ = interfaceIndex;
    selectedDevIndex_ = deviceIndex;
    selectedLabel_ = label;

    captureService_->setCameraFactory([interfaceIndex, deviceIndex]() {
        return std::make_unique<camera::common::EGrabberCamera>(interfaceIndex, deviceIndex);
    });
    SPDLOG_INFO("Hardware camera selected: {} (if={}, dev={})",
                label, interfaceIndex, deviceIndex);
}

bool AppBackend::applyCameraScriptFromFile(const std::string& path, std::string* errorOut) {
    if (selectedIfIndex_ < 0 || selectedDevIndex_ < 0) {
        if (errorOut) *errorOut = "No hardware camera selected";
        return false;
    }
    // Ensure capture thread is stopped
    if (captureService_ && captureService_->isRunning()) {
        SPDLOG_INFO("Stopping capture before applying camera script");
        captureService_->stop();
    }
    SPDLOG_INFO("Applying camera script to {} from {}", selectedLabel_, path);
    return cameraControlService_->applyScriptToDevice(selectedIfIndex_, selectedDevIndex_, path, errorOut);
}

bool AppBackend::resetSelectedHardwareCamera(std::string* errorOut) {
    if (selectedIfIndex_ < 0 || selectedDevIndex_ < 0) {
        if (errorOut) *errorOut = "No hardware camera selected";
        return false;
    }
    // Ensure capture thread is stopped
    if (captureService_ && captureService_->isRunning()) {
        SPDLOG_INFO("Stopping capture before camera reset");
        captureService_->stop();
    }
    SPDLOG_INFO("Resetting camera {}", selectedLabel_);
    return cameraControlService_->deviceReset(selectedIfIndex_, selectedDevIndex_, errorOut);
}

} // namespace backend

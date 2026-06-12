#include "backend/app/AppBackend.h"
#include "backend/app/BackendFacade.h"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
    std::filesystem::path makeTempDir()
    {
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<unsigned long long> dist;
        for (int attempt = 0; attempt < 100; ++attempt)
        {
            const auto path = std::filesystem::temp_directory_path() /
                              ("mib_backend_facade_" + std::to_string(dist(gen)));
            std::error_code ec;
            if (std::filesystem::create_directories(path, ec))
            {
                return path;
            }
        }
        throw std::runtime_error("failed to create temporary directory");
    }

    void setEnv(const char *name, const char *value)
    {
#ifdef _WIN32
        _putenv_s(name, value);
#else
        setenv(name, value, 1);
#endif
    }

    template <typename Event>
    bool hasEvent(const std::vector<backend::bridge::BackendEvent> &events)
    {
        for (const auto &event : events)
        {
            if (std::holds_alternative<Event>(event))
            {
                return true;
            }
        }
        return false;
    }

    bool waitForPlaybackFrame(backend::bridge::BackendFacade &facade,
                              backend::bridge::BackendFrame &out)
    {
        for (int attempt = 0; attempt < 50; ++attempt)
        {
            if (facade.fetchLatestFrame(out))
            {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }
} // namespace

int main()
{
    namespace bridge = backend::bridge;

    const auto dataDir = makeTempDir();
    const auto mockDir = dataDir / "mock_frames";
    std::filesystem::create_directories(mockDir);

    const cv::Mat frame(16, 16, CV_8UC1, cv::Scalar(180));
    const auto mockFramePath = mockDir / "frame_000.tiff";
    if (!cv::imwrite(mockFramePath.string(), frame))
    {
        std::cerr << "failed to write mock frame fixture\n";
        std::error_code cleanupError;
        std::filesystem::remove_all(dataDir, cleanupError);
        return 1;
    }

    setEnv("MIB_CAMERA_MODE", "mock");
    setEnv("MIB_MOCK_CAMERA_DIR", mockDir.string().c_str());
    setEnv("MIB_MOCK_CAMERA_INTERVAL_MS", "1");
    setEnv("MIB_MOCK_CAMERA_LOOP", "true");

    const int result = [&]() -> int {
        backend::AppBackend backend;
        bridge::BackendFacade facade(backend);
        std::vector<bridge::BackendEvent> events;
        facade.setEventSink([&events](const bridge::BackendEvent &event) {
            events.push_back(event);
        });

        if (!facade.initialize(dataDir.string()) || !facade.isInitialized())
        {
            std::cerr << "BackendFacade should initialize AppBackend explicitly\n";
            return 2;
        }

        bridge::ProcessingSettingsCommand processingCommand;
        auto config = backend.processing().getProcessingConfig();
        config.empty_frame_pixel_threshold = 12;
        processingCommand.config = config;
        processingCommand.roi = backend::services::ProcessingService::Roi{1, 2, 8, 9};
        processingCommand.realtimeEnabled = false;
        processingCommand.realtimeDropFrames = true;
        processingCommand.pixelToMicronFactor = 2.5;
        if (!facade.dispatch(processingCommand).ok)
        {
            std::cerr << "ProcessingSettingsCommand should apply through ProcessingService\n";
            return 3;
        }
        if (backend.processing().getProcessingConfig().empty_frame_pixel_threshold != 12 ||
            backend.processing().getRealtimeRoi().w != 8 ||
            !backend.processing().getRealtimeDropFrames() ||
            backend.processing().getPixelToMicronFactor() != 2.5)
        {
            std::cerr << "Processing settings were not delegated to ProcessingService\n";
            return 4;
        }

    bridge::CameraCommand configureMock;
    configureMock.action = bridge::CameraCommandAction::ConfigureMockCamera;
    configureMock.mockFrameDirectory = mockDir.string();
    configureMock.mockFrameIntervalMs = 1;
    configureMock.mockLoopFiles = true;
    if (!facade.dispatch(configureMock).ok)
    {
        std::cerr << "CameraCommand should configure mock camera through AppBackend\n";
        return 5;
    }

    bridge::CameraCommand startCapture;
    startCapture.action = bridge::CameraCommandAction::StartCapture;
    if (!facade.dispatch(startCapture).ok)
    {
        std::cerr << "CameraCommand should start capture through CaptureService\n";
        return 6;
    }

    bridge::BackendFrame latestFrame;
    if (!waitForPlaybackFrame(facade, latestFrame) || latestFrame.data.empty())
    {
        std::cerr << "BackendFacade should expose Qt-widget-free frame copies\n";
        facade.shutdown();
        return 7;
    }

    bridge::PlaybackSeekCommand seekLatest;
    seekLatest.mode = bridge::PlaybackSeekMode::Latest;
    if (!facade.dispatch(seekLatest).ok)
    {
        std::cerr << "PlaybackSeekCommand should resolve through PlaybackService\n";
        facade.shutdown();
        return 8;
    }

    const auto recordingPath = dataDir / "bridge_recording.h5";
    bridge::RecordingCommand startRecording;
    startRecording.action = bridge::RecordingCommandAction::StartFrameRecording;
    startRecording.filePath = recordingPath.string();
    if (!facade.dispatch(startRecording).ok)
    {
        std::cerr << "RecordingCommand should start frame recording through AppBackend\n";
        facade.shutdown();
        return 9;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));

    bridge::RecordingCommand stopRecording;
    stopRecording.action = bridge::RecordingCommandAction::StopFrameRecording;
    stopRecording.filePath = recordingPath.string();
    if (!facade.dispatch(stopRecording).ok)
    {
        std::cerr << "RecordingCommand should stop frame recording through AppBackend\n";
        facade.shutdown();
        return 10;
    }

    bridge::RecordingLoadCommand loadRecording;
    loadRecording.filePath = recordingPath.string();
    if (!facade.dispatch(loadRecording).ok)
    {
        std::cerr << "RecordingLoadCommand should load recorded HDF5 through Hdf5Service\n";
        facade.shutdown();
        return 11;
    }

    bridge::CameraCommand stopCapture;
    stopCapture.action = bridge::CameraCommandAction::StopCapture;
    if (!facade.dispatch(stopCapture).ok)
    {
        std::cerr << "CameraCommand should stop capture through CaptureService\n";
        facade.shutdown();
        return 12;
    }

    facade.shutdown();
    if (facade.isInitialized())
    {
        std::cerr << "BackendFacade shutdown should make lifecycle explicit\n";
        return 13;
    }

    if (!hasEvent<bridge::CameraStatusEvent>(events) ||
        !hasEvent<bridge::PlaybackPositionEvent>(events) ||
        !hasEvent<bridge::FrameReadyEvent>(events) ||
        !hasEvent<bridge::RecordingStatusEvent>(events) ||
        !hasEvent<bridge::ProcessingResultEvent>(events))
    {
        std::cerr << "BackendFacade should emit frontend-neutral event variants\n";
        return 14;
    }

        return 0;
    }();

    std::error_code cleanupError;
    std::filesystem::remove_all(dataDir, cleanupError);
    return result;
}

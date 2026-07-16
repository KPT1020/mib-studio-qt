#include "backend/app/BackendFacade.h"

#include "backend/app/AppBackend.h"
#include "backend/app/BackgroundFrame.h"
#include "backend/camera/mock/MockCamera.h"
#include "backend/playback/FrameStore.h"
#include "backend/playback/PlaybackService.h"
#include "backend/recording/Hdf5Service.h"
#include "backend/services/CaptureService.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <type_traits>
#include <utility>

namespace backend::bridge
{
    namespace
    {
        BackendCommandType commandType(const BackendCommand &command)
        {
            return std::visit([](const auto &typedCommand) -> BackendCommandType {
                using Command = std::decay_t<decltype(typedCommand)>;
                if constexpr (std::is_same_v<Command, CameraCommand>)
                {
                    return BackendCommandType::Camera;
                }
                else if constexpr (std::is_same_v<Command, RecordingCommand>)
                {
                    return BackendCommandType::Recording;
                }
                else if constexpr (std::is_same_v<Command, ProcessingSettingsCommand>)
                {
                    return BackendCommandType::ProcessingSettings;
                }
                else if constexpr (std::is_same_v<Command, RecordingLoadCommand>)
                {
                    return BackendCommandType::RecordingLoad;
                }
                else if constexpr (std::is_same_v<Command, PlaybackSeekCommand>)
                {
                    return BackendCommandType::PlaybackSeek;
                }
                else
                {
                    return BackendCommandType::Operation;
                }
            }, command);
        }

        FrameReadyEvent makePlaybackFrameReady(std::uint64_t frameIndex,
                                               const playback::Frame &frame)
        {
            return FrameReadyEvent{
                FrameReadySource::Playback,
                frameIndex,
                frame.timestamp,
                frame.width,
                frame.height,
                frame.pixelFormat,
                frame.linePitch,
                frame.data.size(),
            };
        }

        BackendFrame makeBackendFrame(std::uint64_t frameIndex,
                                      playback::Frame frame)
        {
            BackendFrame out;
            out.frameIndex = frameIndex;
            out.timestampNs = frame.timestamp;
            out.width = frame.width;
            out.height = frame.height;
            out.pixelFormat = frame.pixelFormat;
            out.strideBytes = frame.linePitch;
            out.data = std::move(frame.data);
            return out;
        }

        BackendErrorSource errorSourceForCommand(BackendCommandType command)
        {
            switch (command)
            {
            case BackendCommandType::Camera:
                return BackendErrorSource::Camera;
            case BackendCommandType::Recording:
            case BackendCommandType::RecordingLoad:
                return BackendErrorSource::Recording;
            case BackendCommandType::ProcessingSettings:
                return BackendErrorSource::Processing;
            case BackendCommandType::PlaybackSeek:
                return BackendErrorSource::Playback;
            case BackendCommandType::Operation:
                return BackendErrorSource::Lifecycle;
            }
            return BackendErrorSource::Lifecycle;
        }

        std::uint64_t atomicLoad(const std::atomic<std::uint64_t> &value)
        {
            return value.load(std::memory_order_relaxed);
        }
    } // namespace

    BackendFacade::BackendFacade(AppBackend &backend)
        : backend_(backend)
    {
    }

    BackendFacade::~BackendFacade()
    {
        backend_.setBackgroundCaptureCallback({});
    }

    bool BackendFacade::initialize(const std::string &dataDir)
    {
        if (initialized_)
        {
            return true;
        }

        if (!backend_.initialize(dataDir))
        {
            emitEvent(BackendErrorEvent{
                BackendErrorSource::Lifecycle,
                BackendCommandType::Camera,
                "Backend initialization failed",
            });
            return false;
        }

        initialized_ = true;
        emitEvent(makeCameraStatus(backend_.isCameraConfigured()
                                       ? CameraState::Configured
                                       : CameraState::Unconfigured));
        return true;
    }

    void BackendFacade::shutdown()
    {
        if (!initialized_)
        {
            return;
        }

        // Drain/cancel tracked operations first so their consumers see a
        // terminal event before service teardown (BE-1 shutdown policy).
        cancelAllOperations("Backend shutdown");

        backend_.stopFrameRecording();
        backend_.capture().stop();
        backend_.processing().stopRealtime();
        backend_.processing().stop();
        if (backend_.hdf5().isFileOpen())
        {
            backend_.hdf5().closeFile();
        }
        initialized_ = false;

        emitEvent(RecordingStatusEvent{
            RecordingState::Stopped,
            {},
            false,
            backend_.frameRecordingCount(),
            backend_.frameRecordingFiltered(),
            0,
            0,
        });
        emitEvent(makeCameraStatus(CameraState::Stopped));
    }

    bool BackendFacade::isInitialized() const
    {
        return initialized_;
    }

    void BackendFacade::setEventSink(EventSink sink)
    {
        bool hasSink = false;
        {
            std::scoped_lock lock(eventSinkMutex_);
            eventSink_ = std::move(sink);
            hasSink = static_cast<bool>(eventSink_);
        }

        if (!hasSink)
        {
            backend_.setBackgroundCaptureCallback({});
            return;
        }

        backend_.setBackgroundCaptureCallback([this](const BackgroundCaptureEvent &event) {
            if (event.frame.empty())
            {
                return;
            }

            emitEvent(FrameReadyEvent{
                FrameReadySource::BackgroundCapture,
                event.frameIndex,
                0,
                event.frame.width,
                event.frame.height,
                static_cast<std::uint64_t>(event.frame.pixelFormat),
                event.frame.strideBytes,
                event.frame.data.size(),
            });
        });
    }

    BackendCommandResult BackendFacade::dispatch(const BackendCommand &command)
    {
        if (!initialized_)
        {
            return lifecycleError(commandType(command), "Backend facade is not initialized");
        }

        return std::visit([this](const auto &typedCommand) {
            using Command = std::decay_t<decltype(typedCommand)>;
            if constexpr (std::is_same_v<Command, CameraCommand>)
            {
                return handleCameraCommand(typedCommand);
            }
            else if constexpr (std::is_same_v<Command, RecordingCommand>)
            {
                return handleRecordingCommand(typedCommand);
            }
            else if constexpr (std::is_same_v<Command, ProcessingSettingsCommand>)
            {
                return handleProcessingSettingsCommand(typedCommand);
            }
            else if constexpr (std::is_same_v<Command, RecordingLoadCommand>)
            {
                return handleRecordingLoadCommand(typedCommand);
            }
            else if constexpr (std::is_same_v<Command, PlaybackSeekCommand>)
            {
                return handlePlaybackSeekCommand(typedCommand);
            }
            else
            {
                return handleOperationCommand(typedCommand);
            }
        }, command);
    }

    bool BackendFacade::fetchLatestFrame(BackendFrame &out) const
    {
        if (!initialized_)
        {
            return false;
        }

        std::uint64_t earliest = 0;
        std::uint64_t latest = 0;
        std::size_t count = 0;
        if (!backend_.playback().queryRange(earliest, latest, count))
        {
            return false;
        }

        playback::Frame frame;
        if (!backend_.playback().fetchLatest(frame))
        {
            return false;
        }

        out = makeBackendFrame(latest, std::move(frame));
        return true;
    }

    bool BackendFacade::fetchFrameByIndex(std::uint64_t frameIndex, BackendFrame &out) const
    {
        if (!initialized_)
        {
            return false;
        }

        playback::Frame frame;
        if (!backend_.playback().fetchByIndex(frameIndex, frame))
        {
            return false;
        }

        out = makeBackendFrame(frameIndex, std::move(frame));
        return true;
    }

    bool BackendFacade::fetchProcessingStats(BackendProcessingStats &out) const
    {
        if (!initialized_)
        {
            return false;
        }

        auto &processing = backend_.processing();
        out.algoFps1s = processing.getAlgoFps1s();
        out.validFps1s = processing.getValidFps1s();
        out.invalidFps1s = processing.getInvalidFps1s();
        out.pixelToMicronFactor = processing.getPixelToMicronFactor();
        return true;
    }

    BackendCommandResult BackendFacade::handleCameraCommand(const CameraCommand &command)
    {
        switch (command.action)
        {
        case CameraCommandAction::ConfigureMockCamera:
        {
            camera::mock::MockCameraOptions options;
            options.folder = std::filesystem::path(command.mockFrameDirectory);
            options.frameInterval = std::chrono::milliseconds(std::max(1, command.mockFrameIntervalMs));
            options.loopFiles = command.mockLoopFiles;
            backend_.configureMockCamera(options);
            emitEvent(makeCameraStatus(CameraState::Configured, "mock"));
            return {true, BackendCommandType::Camera, "Mock camera configured"};
        }
        case CameraCommandAction::SelectHardwareCamera:
            backend_.setHardwareCameraSelection(command.hardwareInterfaceIndex,
                                                command.hardwareDeviceIndex,
                                                command.hardwareLabel);
            emitEvent(makeCameraStatus(CameraState::Configured, command.hardwareLabel));
            return {true, BackendCommandType::Camera, "Hardware camera selected"};
        case CameraCommandAction::SelectMindVisionCamera:
        {
            backend_.setMindVisionCameraSelection(command.mindVisionCameraIndex,
                                                 command.mindVisionLabel);
            if (!command.mindVisionConfigPath.empty())
            {
                std::string error;
                if (!backend_.applyMindVisionConfigFromFile(command.mindVisionConfigPath, &error))
                {
                    const std::string message = error.empty() ? "MindVision config apply failed" : error;
                    emitEvent(BackendErrorEvent{BackendErrorSource::Camera, BackendCommandType::Camera, message});
                    return {false, BackendCommandType::Camera, message};
                }
            }
            emitEvent(makeCameraStatus(CameraState::Configured, command.mindVisionLabel));
            return {true, BackendCommandType::Camera, "MindVision camera selected"};
        }
        case CameraCommandAction::ApplyCameraScript:
        {
            std::string error;
            if (!backend_.applyCameraScriptFromFile(command.cameraScriptPath, &error))
            {
                const std::string message = error.empty() ? "Camera script apply failed" : error;
                emitEvent(BackendErrorEvent{BackendErrorSource::Camera, BackendCommandType::Camera, message});
                return {false, BackendCommandType::Camera, message};
            }
            emitEvent(makeCameraStatus(CameraState::Configured));
            return {true, BackendCommandType::Camera, "Camera script applied"};
        }
        case CameraCommandAction::ResetSelectedHardwareCamera:
        {
            std::string error;
            if (!backend_.resetSelectedHardwareCamera(&error))
            {
                const std::string message = error.empty() ? "Camera reset failed" : error;
                emitEvent(BackendErrorEvent{BackendErrorSource::Camera, BackendCommandType::Camera, message});
                return {false, BackendCommandType::Camera, message};
            }
            emitEvent(makeCameraStatus(CameraState::Configured));
            return {true, BackendCommandType::Camera, "Camera reset requested"};
        }
        case CameraCommandAction::StartCapture:
            emitEvent(makeCameraStatus(CameraState::Starting));
            if (!backend_.capture().start())
            {
                emitEvent(makeCameraStatus(CameraState::Error));
                emitEvent(BackendErrorEvent{BackendErrorSource::Camera,
                                            BackendCommandType::Camera,
                                            "Capture start failed"});
                return {false, BackendCommandType::Camera, "Capture start failed"};
            }
            emitEvent(makeCameraStatus(CameraState::Running));
            return {true, BackendCommandType::Camera, "Capture started"};
        case CameraCommandAction::StopCapture:
            backend_.capture().stop();
            emitEvent(makeCameraStatus(CameraState::Stopped));
            return {true, BackendCommandType::Camera, "Capture stopped"};
        }

        emitEvent(BackendErrorEvent{BackendErrorSource::Camera,
                                    BackendCommandType::Camera,
                                    "Unknown camera command"});
        return {false, BackendCommandType::Camera, "Unknown camera command"};
    }

    BackendCommandResult BackendFacade::handleRecordingCommand(const RecordingCommand &command)
    {
        switch (command.action)
        {
        case RecordingCommandAction::StartFrameRecording:
            emitEvent(RecordingStatusEvent{
                RecordingState::Starting,
                command.filePath,
                false,
                backend_.frameRecordingCount(),
                backend_.frameRecordingFiltered(),
                0,
                0,
            });
            if (!backend_.startFrameRecording(command.filePath))
            {
                emitEvent(RecordingStatusEvent{
                    RecordingState::Error,
                    command.filePath,
                    false,
                    backend_.frameRecordingCount(),
                    backend_.frameRecordingFiltered(),
                    0,
                    0,
                });
                emitEvent(BackendErrorEvent{BackendErrorSource::Recording,
                                            BackendCommandType::Recording,
                                            "Frame recording start failed"});
                return {false, BackendCommandType::Recording, "Frame recording start failed"};
            }
            emitEvent(RecordingStatusEvent{
                RecordingState::Recording,
                command.filePath,
                false,
                backend_.frameRecordingCount(),
                backend_.frameRecordingFiltered(),
                0,
                0,
            });
            return {true, BackendCommandType::Recording, "Frame recording started"};
        case RecordingCommandAction::StopFrameRecording:
            backend_.stopFrameRecording();
            emitEvent(RecordingStatusEvent{
                RecordingState::Stopped,
                command.filePath,
                false,
                backend_.frameRecordingCount(),
                backend_.frameRecordingFiltered(),
                0,
                0,
            });
            return {true, BackendCommandType::Recording, "Frame recording stopped"};
        }

        emitEvent(BackendErrorEvent{BackendErrorSource::Recording,
                                    BackendCommandType::Recording,
                                    "Unknown recording command"});
        return {false, BackendCommandType::Recording, "Unknown recording command"};
    }

    BackendCommandResult BackendFacade::handleProcessingSettingsCommand(const ProcessingSettingsCommand &command)
    {
        auto &processing = backend_.processing();
        if (command.config)
        {
            processing.setProcessingConfig(*command.config);
        }
        if (command.roi)
        {
            processing.setRealtimeRoi(*command.roi);
        }
        if (command.realtimeEnabled)
        {
            processing.setRealtimeEnabled(*command.realtimeEnabled);
        }
        if (command.realtimeDropFrames)
        {
            processing.setRealtimeDropFrames(*command.realtimeDropFrames);
        }
        if (command.realtimeProcessingMode)
        {
            processing.setRealtimeProcessingMode(*command.realtimeProcessingMode);
        }
        if (command.realtimeBatchSettings)
        {
            processing.setRealtimeBatchSettings(*command.realtimeBatchSettings);
        }
        if (command.pixelToMicronFactor)
        {
            processing.setPixelToMicronFactor(*command.pixelToMicronFactor);
        }

        emitEvent(ProcessingResultEvent{
            0,
            0,
            processing.getAlgoFps1s(),
            processing.getValidFps1s(),
            processing.getInvalidFps1s(),
            {},
        });
        return {true, BackendCommandType::ProcessingSettings, "Processing settings applied"};
    }

    BackendCommandResult BackendFacade::handleRecordingLoadCommand(const RecordingLoadCommand &command)
    {
        // Tracked as an operation (BE-1): synchronous today, but the shell
        // already correlates Started/terminal events by operationId so the
        // load can move off-thread without a contract change.
        const std::uint64_t operationId =
            beginOperation(BackendOperationKind::RecordingLoad, nullptr, command.filePath);

        auto &hdf5 = backend_.hdf5();
        if (!hdf5.loadFile(command.filePath))
        {
            emitEvent(RecordingStatusEvent{
                RecordingState::Error,
                command.filePath,
                false,
                0,
                0,
                0,
                0,
            });
            emitEvent(BackendErrorEvent{BackendErrorSource::Recording,
                                        BackendCommandType::RecordingLoad,
                                        "Recording load failed"});
            finishOperation(operationId, BackendOperationState::Failed, "Recording load failed");
            return {false, BackendCommandType::RecordingLoad, "Recording load failed", operationId};
        }

        RecordingStatusEvent event;
        event.state = RecordingState::Loaded;
        event.filePath = command.filePath;

        if (command.readMetadata)
        {
            event.recordingFile = hdf5.isRecordingFile();
            if (event.recordingFile)
            {
                std::uint64_t startTimeNs = 0;
                std::uint64_t endTimeNs = 0;
                std::uint64_t totalFrames = 0;
                std::uint64_t filteredFrames = 0;
                if (hdf5.readRecordingInfo(startTimeNs, endTimeNs, totalFrames, filteredFrames))
                {
                    event.framesWritten = totalFrames;
                    event.framesFiltered = filteredFrames;
                }

                std::vector<services::ProcessedFrame> frames;
                if (hdf5.readRecordingMetadata(frames))
                {
                    event.loadedValidFrames = frames.size();
                }
            }
            else
            {
                std::vector<services::ProcessedFrame> validFrames;
                std::vector<services::ProcessedFrame> invalidFrames;
                if (hdf5.readValidMetadata(validFrames))
                {
                    event.loadedValidFrames = validFrames.size();
                }
                if (hdf5.readInvalidMetadata(invalidFrames))
                {
                    event.loadedInvalidFrames = invalidFrames.size();
                }
            }
        }

        emitEvent(event);
        finishOperation(operationId, BackendOperationState::Completed, command.filePath);
        return {true, BackendCommandType::RecordingLoad, "Recording loaded", operationId};
    }

    BackendCommandResult BackendFacade::handleOperationCommand(const OperationCommand &command)
    {
        switch (command.action)
        {
        case OperationCommandAction::Cancel:
            if (requestOperationCancel(command.operationId))
            {
                return {true, BackendCommandType::Operation, "Operation cancel requested",
                        command.operationId};
            }
            // Unknown or already-finished IDs fail safely without touching
            // state — duplicate cancels cannot desynchronize anything.
            return {false, BackendCommandType::Operation,
                    "Unknown or finished operation", command.operationId};
        }

        return {false, BackendCommandType::Operation, "Unknown operation command"};
    }

    BackendCommandResult BackendFacade::handlePlaybackSeekCommand(const PlaybackSeekCommand &command)
    {
        auto &playback = backend_.playback();

        std::uint64_t earliest = 0;
        std::uint64_t latest = 0;
        std::size_t count = 0;
        const bool hasRange = playback.queryRange(earliest, latest, count);
        const std::uint64_t desiredIndex =
            command.mode == PlaybackSeekMode::Latest ? latest : command.frameIndex;

        playback::Frame frame;
        const bool hasFrame = hasRange && playback.fetchByIndex(desiredIndex, frame);
        PlaybackPositionEvent position;
        position.hasFrame = hasFrame;
        position.playing = playback.isPlaying();
        position.frameIndex = desiredIndex;
        position.timestampNs = hasFrame ? frame.timestamp : 0;
        position.earliestIndex = earliest;
        position.latestIndex = latest;
        position.availableCount = count;

        emitEvent(position);
        if (!hasFrame)
        {
            return {false, BackendCommandType::PlaybackSeek, "Playback frame not available"};
        }

        emitEvent(makePlaybackFrameReady(desiredIndex, frame));
        return {true, BackendCommandType::PlaybackSeek, "Playback seek resolved"};
    }

    BackendCommandResult BackendFacade::lifecycleError(BackendCommandType command, const std::string &message)
    {
        emitEvent(BackendErrorEvent{
            errorSourceForCommand(command),
            command,
            message,
        });
        return {false, command, message};
    }

    void BackendFacade::emitEvent(const BackendEvent &event) const
    {
        EventSink sink;
        {
            std::scoped_lock lock(eventSinkMutex_);
            sink = eventSink_;
        }
        if (sink)
        {
            sink(event);
        }
    }

    std::uint64_t BackendFacade::beginOperation(BackendOperationKind kind,
                                                CancelFlag *cancelFlagOut,
                                                const std::string &message)
    {
        const std::uint64_t operationId = nextOperationId_.fetch_add(1, std::memory_order_relaxed);
        auto flag = std::make_shared<std::atomic<bool>>(false);
        {
            std::scoped_lock lock(operationsMutex_);
            activeOperations_[operationId] = ActiveOperation{kind, flag};
        }
        if (cancelFlagOut)
        {
            *cancelFlagOut = flag;
        }
        emitEvent(OperationStatusEvent{operationId, kind, BackendOperationState::Started, 0, 0, message});
        return operationId;
    }

    void BackendFacade::reportOperationProgress(std::uint64_t operationId,
                                                std::uint64_t progress,
                                                std::uint64_t total,
                                                const std::string &message)
    {
        BackendOperationKind kind;
        {
            std::scoped_lock lock(operationsMutex_);
            const auto it = activeOperations_.find(operationId);
            if (it == activeOperations_.end())
            {
                // Late progress after a terminal state: dropped by policy.
                return;
            }
            kind = it->second.kind;
        }
        emitEvent(OperationStatusEvent{operationId, kind, BackendOperationState::Progress,
                                       progress, total, message});
    }

    void BackendFacade::finishOperation(std::uint64_t operationId,
                                        BackendOperationState terminalState,
                                        const std::string &message)
    {
        BackendOperationKind kind;
        {
            std::scoped_lock lock(operationsMutex_);
            const auto it = activeOperations_.find(operationId);
            if (it == activeOperations_.end())
            {
                // First terminal state wins; later finishes are late events
                // and are dropped (BE-1 policy).
                return;
            }
            kind = it->second.kind;
            activeOperations_.erase(it);
        }
        emitEvent(OperationStatusEvent{operationId, kind, terminalState, 0, 0, message});
    }

    bool BackendFacade::requestOperationCancel(std::uint64_t operationId)
    {
        std::scoped_lock lock(operationsMutex_);
        const auto it = activeOperations_.find(operationId);
        if (it == activeOperations_.end())
        {
            return false;
        }
        it->second.cancelRequested->store(true, std::memory_order_relaxed);
        return true;
    }

    std::size_t BackendFacade::activeOperationCount() const
    {
        std::scoped_lock lock(operationsMutex_);
        return activeOperations_.size();
    }

    void BackendFacade::cancelAllOperations(const std::string &reason)
    {
        std::vector<std::pair<std::uint64_t, ActiveOperation>> drained;
        {
            std::scoped_lock lock(operationsMutex_);
            drained.assign(activeOperations_.begin(), activeOperations_.end());
            activeOperations_.clear();
        }
        for (auto &[operationId, op] : drained)
        {
            op.cancelRequested->store(true, std::memory_order_relaxed);
            emitEvent(OperationStatusEvent{operationId, op.kind,
                                           BackendOperationState::Cancelled, 0, 0, reason});
        }
    }

    CameraStatusEvent BackendFacade::makeCameraStatus(CameraState state, std::string label) const
    {
        const auto &stats = backend_.capture().stats();
        return CameraStatusEvent{
            state,
            backend_.isCameraConfigured(),
            backend_.capture().isRunning(),
            atomicLoad(stats.framesProcessed),
            atomicLoad(stats.lastFrameRate),
            atomicLoad(stats.lastDataRateMBps),
            std::move(label),
        };
    }

} // namespace backend::bridge

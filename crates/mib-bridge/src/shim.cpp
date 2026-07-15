// C++ side of the Rust <-> C++ bridge (epic #246, ADR 0003). See shim.h.
#include "mib-bridge/src/shim.h"

// cxx-generated definitions of the shared structs (BridgeCommandResult,
// BridgeEvent, BridgeFrame, BridgeEventKind).
#include "mib-bridge/src/lib.rs.h"

#include "backend/app/AppBackend.h"
#include "backend/app/BackendFacade.h"

#include <mutex>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mib_bridge {

namespace {

std::string toStd(rust::Str s) { return std::string(s.data(), s.size()); }

BridgeCommandResult toBridgeResult(const backend::bridge::BackendCommandResult& r) {
    BridgeCommandResult out;
    out.ok = r.ok;
    out.command = static_cast<std::uint32_t>(r.command);
    out.message = rust::String(r.message);
    return out;
}

BridgeCommandResult errorResult(const std::string& message) {
    BridgeCommandResult out;
    out.ok = false;
    out.command = static_cast<std::uint32_t>(backend::bridge::BackendCommandType::Camera);
    out.message = rust::String(message);
    return out;
}

// Flatten a BackendEvent into the typed-slot BridgeEvent. The slot->field
// mapping per kind is documented inline; it is part of the ADR 0003 contract.
BridgeEvent toBridgeEvent(const backend::bridge::BackendEvent& ev) {
    using namespace backend::bridge;
    BridgeEvent out{};
    std::visit(
        [&](const auto& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, FrameReadyEvent>) {
                // u0 index, u1 tsNs, u2 width, u3 height, u4 pixelFormat,
                // u5 strideBytes; f0 byteSize, f1 source.
                out.kind = BridgeEventKind::FrameReady;
                out.u0 = e.frameIndex;
                out.u1 = e.timestampNs;
                out.u2 = e.width;
                out.u3 = e.height;
                out.u4 = e.pixelFormat;
                out.u5 = static_cast<std::uint64_t>(e.strideBytes);
                out.f0 = static_cast<double>(e.byteSize);
                out.f1 = static_cast<double>(static_cast<int>(e.source));
            } else if constexpr (std::is_same_v<T, CameraStatusEvent>) {
                // u0 state, u1 framesProcessed, u2 frameRate, u3 dataRateMBps;
                // b0 configured, b1 running; text label.
                out.kind = BridgeEventKind::CameraStatus;
                out.u0 = static_cast<std::uint64_t>(e.state);
                out.u1 = e.framesProcessed;
                out.u2 = e.frameRate;
                out.u3 = e.dataRateMBps;
                out.b0 = e.configured;
                out.b1 = e.running;
                out.text = rust::String(e.label);
            } else if constexpr (std::is_same_v<T, RecordingStatusEvent>) {
                // u0 state, u1 framesWritten, u2 framesFiltered,
                // u3 loadedValid, u4 loadedInvalid; b0 recordingFile; text path.
                out.kind = BridgeEventKind::RecordingStatus;
                out.u0 = static_cast<std::uint64_t>(e.state);
                out.u1 = e.framesWritten;
                out.u2 = e.framesFiltered;
                out.u3 = e.loadedValidFrames;
                out.u4 = e.loadedInvalidFrames;
                out.b0 = e.recordingFile;
                out.text = rust::String(e.filePath);
            } else if constexpr (std::is_same_v<T, ProcessingResultEvent>) {
                // u0 index, u1 tsNs, u2 objectCount; f0/f1/f2 algo/valid/invalid
                // fps. Per-object detail is omitted at schema v1.
                out.kind = BridgeEventKind::ProcessingResult;
                out.u0 = e.frameIndex;
                out.u1 = e.timestampNs;
                out.u2 = static_cast<std::uint64_t>(e.objects.size());
                out.f0 = e.algoFps1s;
                out.f1 = e.validFps1s;
                out.f2 = e.invalidFps1s;
            } else if constexpr (std::is_same_v<T, PlaybackPositionEvent>) {
                // u0 index, u1 tsNs, u2 earliest, u3 latest, u4 available;
                // b0 hasFrame, b1 playing.
                out.kind = BridgeEventKind::PlaybackPosition;
                out.u0 = e.frameIndex;
                out.u1 = e.timestampNs;
                out.u2 = e.earliestIndex;
                out.u3 = e.latestIndex;
                out.u4 = static_cast<std::uint64_t>(e.availableCount);
                out.b0 = e.hasFrame;
                out.b1 = e.playing;
            } else if constexpr (std::is_same_v<T, BackendErrorEvent>) {
                // u0 source, u1 command; text message.
                out.kind = BridgeEventKind::BackendError;
                out.u0 = static_cast<std::uint64_t>(e.source);
                out.u1 = static_cast<std::uint64_t>(e.command);
                out.text = rust::String(e.message);
            }
        },
        ev);
    return out;
}

} // namespace

struct BackendBridge::Impl {
    backend::AppBackend app;
    backend::bridge::BackendFacade facade{app};
    std::mutex queueMutex;
    std::vector<BridgeEvent> queue;

    void installSink() {
        facade.setEventSink([this](const backend::bridge::BackendEvent& ev) {
            // Runs on backend threads. Do the minimum: convert + enqueue, then
            // return. Rust drains via poll_events (ADR 0003 threading rule).
            BridgeEvent be = toBridgeEvent(ev);
            std::scoped_lock lock(queueMutex);
            queue.push_back(std::move(be));
        });
    }
};

BackendBridge::BackendBridge() : impl_(std::make_unique<Impl>()) {}

BackendBridge::~BackendBridge() {
    if (impl_ && impl_->facade.isInitialized()) {
        impl_->facade.shutdown();
    }
}

bool BackendBridge::initialize(rust::Str data_dir) {
    try {
        if (!impl_->facade.initialize(toStd(data_dir))) {
            return false;
        }
        impl_->installSink();
        return true;
    } catch (...) {
        return false;
    }
}

void BackendBridge::shutdown() {
    try {
        impl_->facade.shutdown();
    } catch (...) {
    }
}

bool BackendBridge::is_initialized() const { return impl_->facade.isInitialized(); }

BridgeCommandResult BackendBridge::configure_mock_camera(rust::Str frame_dir,
                                                         std::int32_t frame_interval_ms,
                                                         bool loop_files) {
    try {
        backend::bridge::CameraCommand cmd;
        cmd.action = backend::bridge::CameraCommandAction::ConfigureMockCamera;
        cmd.mockFrameDirectory = toStd(frame_dir);
        cmd.mockFrameIntervalMs = frame_interval_ms;
        cmd.mockLoopFiles = loop_files;
        return toBridgeResult(impl_->facade.dispatch(cmd));
    } catch (const std::exception& e) {
        return errorResult(std::string("configure_mock_camera: ") + e.what());
    } catch (...) {
        return errorResult("configure_mock_camera: unknown error");
    }
}

BridgeCommandResult BackendBridge::start_capture() {
    try {
        backend::bridge::CameraCommand cmd;
        cmd.action = backend::bridge::CameraCommandAction::StartCapture;
        return toBridgeResult(impl_->facade.dispatch(cmd));
    } catch (const std::exception& e) {
        return errorResult(std::string("start_capture: ") + e.what());
    } catch (...) {
        return errorResult("start_capture: unknown error");
    }
}

BridgeCommandResult BackendBridge::stop_capture() {
    try {
        backend::bridge::CameraCommand cmd;
        cmd.action = backend::bridge::CameraCommandAction::StopCapture;
        return toBridgeResult(impl_->facade.dispatch(cmd));
    } catch (const std::exception& e) {
        return errorResult(std::string("stop_capture: ") + e.what());
    } catch (...) {
        return errorResult("stop_capture: unknown error");
    }
}

BridgeCommandResult BackendBridge::start_frame_recording(rust::Str file_path) {
    try {
        backend::bridge::RecordingCommand cmd;
        cmd.action = backend::bridge::RecordingCommandAction::StartFrameRecording;
        cmd.filePath = toStd(file_path);
        return toBridgeResult(impl_->facade.dispatch(cmd));
    } catch (const std::exception& e) {
        return errorResult(std::string("start_frame_recording: ") + e.what());
    } catch (...) {
        return errorResult("start_frame_recording: unknown error");
    }
}

BridgeCommandResult BackendBridge::stop_frame_recording() {
    try {
        backend::bridge::RecordingCommand cmd;
        cmd.action = backend::bridge::RecordingCommandAction::StopFrameRecording;
        return toBridgeResult(impl_->facade.dispatch(cmd));
    } catch (const std::exception& e) {
        return errorResult(std::string("stop_frame_recording: ") + e.what());
    } catch (...) {
        return errorResult("stop_frame_recording: unknown error");
    }
}

BridgeCommandResult BackendBridge::playback_seek_latest() {
    try {
        backend::bridge::PlaybackSeekCommand cmd;
        cmd.mode = backend::bridge::PlaybackSeekMode::Latest;
        return toBridgeResult(impl_->facade.dispatch(cmd));
    } catch (const std::exception& e) {
        return errorResult(std::string("playback_seek_latest: ") + e.what());
    } catch (...) {
        return errorResult("playback_seek_latest: unknown error");
    }
}

BridgeCommandResult BackendBridge::load_recording(rust::Str file_path) {
    try {
        backend::bridge::RecordingLoadCommand cmd;
        cmd.filePath = toStd(file_path);
        cmd.readMetadata = true;
        return toBridgeResult(impl_->facade.dispatch(cmd));
    } catch (const std::exception& e) {
        return errorResult(std::string("load_recording: ") + e.what());
    } catch (...) {
        return errorResult("load_recording: unknown error");
    }
}

BridgeCommandResult BackendBridge::playback_seek_index(std::uint64_t frame_index) {
    try {
        backend::bridge::PlaybackSeekCommand cmd;
        cmd.mode = backend::bridge::PlaybackSeekMode::AbsoluteIndex;
        cmd.frameIndex = frame_index;
        return toBridgeResult(impl_->facade.dispatch(cmd));
    } catch (const std::exception& e) {
        return errorResult(std::string("playback_seek_index: ") + e.what());
    } catch (...) {
        return errorResult("playback_seek_index: unknown error");
    }
}

BridgeCommandResult BackendBridge::apply_processing(bool realtime_enabled,
                                                    double pixel_to_micron) {
    try {
        backend::bridge::ProcessingSettingsCommand cmd;
        cmd.realtimeEnabled = realtime_enabled;
        cmd.pixelToMicronFactor = pixel_to_micron;
        return toBridgeResult(impl_->facade.dispatch(cmd));
    } catch (const std::exception& e) {
        return errorResult(std::string("apply_processing: ") + e.what());
    } catch (...) {
        return errorResult("apply_processing: unknown error");
    }
}

rust::Vec<BridgeEvent> BackendBridge::poll_events() {
    std::vector<BridgeEvent> drained;
    {
        std::scoped_lock lock(impl_->queueMutex);
        drained.swap(impl_->queue);
    }
    rust::Vec<BridgeEvent> out;
    for (auto& e : drained) {
        out.push_back(std::move(e));
    }
    return out;
}

namespace {

BridgeFrame toBridgeFrame(const backend::bridge::BackendFrame& frame) {
    BridgeFrame out{};
    out.valid = true;
    out.frame_index = frame.frameIndex;
    out.timestamp_ns = frame.timestampNs;
    out.width = frame.width;
    out.height = frame.height;
    out.pixel_format = frame.pixelFormat;
    out.stride_bytes = static_cast<std::uint64_t>(frame.strideBytes);
    out.data.reserve(frame.data.size());
    for (std::uint8_t byte : frame.data) {
        out.data.push_back(byte);
    }
    return out;
}

} // namespace

BridgeFrame BackendBridge::fetch_latest_frame() {
    backend::bridge::BackendFrame frame;
    if (!impl_->facade.fetchLatestFrame(frame)) {
        return BridgeFrame{};
    }
    return toBridgeFrame(frame);
}

BridgeFrame BackendBridge::fetch_frame_by_index(std::uint64_t frame_index) {
    backend::bridge::BackendFrame frame;
    if (!impl_->facade.fetchFrameByIndex(frame_index, frame)) {
        return BridgeFrame{};
    }
    return toBridgeFrame(frame);
}

BridgeProcessingStats BackendBridge::fetch_processing_stats() {
    BridgeProcessingStats out{};
    backend::bridge::BackendProcessingStats stats;
    if (!impl_->facade.fetchProcessingStats(stats)) {
        out.valid = false;
        return out;
    }
    out.valid = true;
    out.algo_fps1s = stats.algoFps1s;
    out.valid_fps1s = stats.validFps1s;
    out.invalid_fps1s = stats.invalidFps1s;
    out.pixel_to_micron = stats.pixelToMicronFactor;
    return out;
}

std::unique_ptr<BackendBridge> new_backend_bridge() {
    return std::make_unique<BackendBridge>();
}

// Schema version of the command/event contract. v2 added the review commands
// (load_recording, playback_seek_index, fetch_frame_by_index); v3 added the
// processing commands (apply_processing, fetch_processing_stats). All additive
// over v1 (ADR 0003).
std::uint32_t bridge_abi_version() { return 3; }

} // namespace mib_bridge

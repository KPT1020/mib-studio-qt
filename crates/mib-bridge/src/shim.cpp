// C++ side of the Rust <-> C++ bridge (epic #246, ADR 0003). See shim.h.
#include "mib-bridge/src/shim.h"

// cxx-generated definitions of the shared structs (BridgeCommandResult,
// BridgeEvent, BridgeFrame, BridgeEventKind).
#include "mib-bridge/src/lib.rs.h"

#include "backend/app/AppBackend.h"
#include "backend/app/BackendFacade.h"
#include "backend/services/CameraControlService.h"

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mib_bridge {

namespace {

// ---- Contract pinning (BE-1, ADR 0004) ----
// These asserts tie the C++ backend enums to the numeric values in
// crates/mib-bridge/contract/bridge-contract.json (mirrored by the cxx enum in
// lib.rs and the generated desktop/src/bridgeContract.ts). Renumbering any of
// them breaks the build here instead of drifting silently.
namespace bb = backend::bridge;

static_assert(std::variant_size_v<bb::BackendEvent> == 8,
              "BackendEvent variant changed — update the bridge contract");
static_assert(std::is_same_v<std::variant_alternative_t<0, bb::BackendEvent>, bb::FrameReadyEvent>);
static_assert(std::is_same_v<std::variant_alternative_t<1, bb::BackendEvent>, bb::CameraStatusEvent>);
static_assert(std::is_same_v<std::variant_alternative_t<2, bb::BackendEvent>, bb::RecordingStatusEvent>);
static_assert(std::is_same_v<std::variant_alternative_t<3, bb::BackendEvent>, bb::ProcessingResultEvent>);
static_assert(std::is_same_v<std::variant_alternative_t<4, bb::BackendEvent>, bb::PlaybackPositionEvent>);
static_assert(std::is_same_v<std::variant_alternative_t<5, bb::BackendEvent>, bb::BackendErrorEvent>);
static_assert(std::is_same_v<std::variant_alternative_t<6, bb::BackendEvent>, bb::OperationStatusEvent>);
static_assert(std::is_same_v<std::variant_alternative_t<7, bb::BackendEvent>, bb::ExperimentStatusEvent>);

static_assert(static_cast<std::uint32_t>(bb::BackendCommandType::Camera) == 0);
static_assert(static_cast<std::uint32_t>(bb::BackendCommandType::Recording) == 1);
static_assert(static_cast<std::uint32_t>(bb::BackendCommandType::ProcessingSettings) == 2);
static_assert(static_cast<std::uint32_t>(bb::BackendCommandType::RecordingLoad) == 3);
static_assert(static_cast<std::uint32_t>(bb::BackendCommandType::PlaybackSeek) == 4);
static_assert(static_cast<std::uint32_t>(bb::BackendCommandType::Operation) == 5);
static_assert(static_cast<std::uint32_t>(bb::BackendCommandType::Experiment) == 6);
static_assert(static_cast<std::uint32_t>(bb::BackendCommandType::Monitoring) == 7);
static_assert(static_cast<std::uint32_t>(bb::BackendCommandType::Trigger) == 8);
static_assert(static_cast<std::uint32_t>(bb::BackendCommandType::Review) == 9);

static_assert(static_cast<std::uint32_t>(bb::ReviewImageDataset::ValidImage) == 0);
static_assert(static_cast<std::uint32_t>(bb::ReviewImageDataset::InvalidImage) == 1);
static_assert(static_cast<std::uint32_t>(bb::ReviewImageDataset::RecordedImage) == 2);
static_assert(static_cast<std::uint32_t>(bb::ReviewImageDataset::ValidMask) == 3);
static_assert(static_cast<std::uint32_t>(bb::ReviewImageDataset::InvalidMask) == 4);

static_assert(static_cast<std::uint32_t>(backend::services::CameraType::EGrabber) == 0);
static_assert(static_cast<std::uint32_t>(backend::services::CameraType::MindVision) == 1);
static_assert(static_cast<std::uint32_t>(backend::AppBackend::CameraSelectionSnapshot::Mode::None) == 0);
static_assert(static_cast<std::uint32_t>(backend::AppBackend::CameraSelectionSnapshot::Mode::Mock) == 1);
static_assert(static_cast<std::uint32_t>(backend::AppBackend::CameraSelectionSnapshot::Mode::Hardware) == 2);
static_assert(static_cast<std::uint32_t>(backend::AppBackend::CameraSelectionSnapshot::Mode::MindVision) == 3);

static_assert(static_cast<std::uint32_t>(backend::ExperimentCoordinator::State::Idle) == 0);
static_assert(static_cast<std::uint32_t>(backend::ExperimentCoordinator::State::Starting) == 1);
static_assert(static_cast<std::uint32_t>(backend::ExperimentCoordinator::State::Active) == 2);
static_assert(static_cast<std::uint32_t>(backend::ExperimentCoordinator::State::Stopping) == 3);
static_assert(static_cast<std::uint32_t>(backend::ExperimentCoordinator::State::Failed) == 4);

static_assert(static_cast<std::uint32_t>(bb::BackendErrorSource::Lifecycle) == 0);
static_assert(static_cast<std::uint32_t>(bb::BackendErrorSource::Playback) == 4);
static_assert(static_cast<std::uint32_t>(bb::BackendErrorSource::Experiment) == 5);
static_assert(static_cast<std::uint32_t>(bb::BackendErrorSource::Monitoring) == 6);
static_assert(static_cast<std::uint32_t>(bb::BackendErrorSource::Hardware) == 7);
static_assert(static_cast<std::uint32_t>(bb::BackendErrorSource::ConfigCore) == 8);
static_assert(static_cast<std::uint32_t>(bb::BackendErrorSource::Review) == 9);
static_assert(static_cast<std::uint32_t>(bb::BackendErrorSource::Export) == 10);
static_assert(static_cast<std::uint32_t>(bb::BackendErrorSource::Platform) == 11);

static_assert(static_cast<std::uint32_t>(bb::BackendOperationKind::RecordingLoad) == 0);
static_assert(static_cast<std::uint32_t>(bb::BackendOperationKind::Reanalysis) == 5);
static_assert(static_cast<std::uint32_t>(bb::BackendOperationState::Started) == 0);
static_assert(static_cast<std::uint32_t>(bb::BackendOperationState::TimedOut) == 5);

std::string toStd(rust::Str s) { return std::string(s.data(), s.size()); }

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

// Event-queue bound (BE-1): the shim queue is drop-oldest bounded so a stalled
// or slow poller cannot grow memory without limit. Overflow is observable via
// a synthetic QueueOverflow event (u0 = dropped since last poll, u1 = dropped
// total) plus queue_overflow_total(). MIB_BRIDGE_MAX_QUEUE overrides the
// capacity (min 4) — used by the bounded-queue contract test.
std::size_t queueCapacityFromEnv() {
    if (const char* raw = std::getenv("MIB_BRIDGE_MAX_QUEUE")) {
        const long parsed = std::strtol(raw, nullptr, 10);
        if (parsed > 0) {
            return std::max<std::size_t>(4, static_cast<std::size_t>(parsed));
        }
    }
    return 4096;
}

BridgeCommandResult toBridgeResult(const backend::bridge::BackendCommandResult& r) {
    BridgeCommandResult out;
    out.ok = r.ok;
    out.command = static_cast<std::uint32_t>(r.command);
    out.message = rust::String(r.message);
    out.operation_id = r.operationId;
    return out;
}

BridgeCommandResult errorResult(const std::string& message) {
    BridgeCommandResult out;
    out.ok = false;
    out.command = static_cast<std::uint32_t>(backend::bridge::BackendCommandType::Camera);
    out.message = rust::String(message);
    out.operation_id = 0;
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
            } else if constexpr (std::is_same_v<T, OperationStatusEvent>) {
                // u0 operationId, u1 kind, u2 state, u3 progress, u4 total;
                // text message.
                out.kind = BridgeEventKind::OperationStatus;
                out.u0 = e.operationId;
                out.u1 = static_cast<std::uint64_t>(e.kind);
                out.u2 = static_cast<std::uint64_t>(e.state);
                out.u3 = e.progress;
                out.u4 = e.total;
                out.text = rust::String(e.message);
            } else if constexpr (std::is_same_v<T, ExperimentStatusEvent>) {
                // u0 state, u1 validBuffered, u2 invalidBuffered,
                // u3 validSaved, u4 invalidSaved, u5 startTimeNs;
                // f0 endTimeNs, f1 droppedValid, f2 droppedInvalid;
                // b0 flushing, b1 cancelled; text message.
                out.kind = BridgeEventKind::ExperimentStatus;
                out.u0 = static_cast<std::uint64_t>(e.state);
                out.u1 = e.validBuffered;
                out.u2 = e.invalidBuffered;
                out.u3 = e.validSaved;
                out.u4 = e.invalidSaved;
                out.u5 = e.startTimeNs;
                out.f0 = static_cast<double>(e.endTimeNs);
                out.f1 = static_cast<double>(e.droppedValid);
                out.f2 = static_cast<double>(e.droppedInvalid);
                out.b0 = e.flushing;
                out.b1 = e.cancelled;
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
    std::deque<BridgeEvent> queue;
    const std::size_t queueCapacity{queueCapacityFromEnv()};
    std::uint64_t droppedSinceLastPoll{0};
    std::uint64_t droppedTotal{0};

    void installSink() {
        facade.setEventSink([this](const backend::bridge::BackendEvent& ev) {
            // Runs on backend threads. Do the minimum: convert + enqueue, then
            // return. Rust drains via poll_events (ADR 0003 threading rule).
            // The queue is bounded drop-oldest (BE-1): high-rate events like
            // FrameReady coalesce to the newest state under a slow poller.
            BridgeEvent be = toBridgeEvent(ev);
            std::scoped_lock lock(queueMutex);
            queue.push_back(std::move(be));
            while (queue.size() > queueCapacity) {
                queue.pop_front();
                ++droppedSinceLastPoll;
                ++droppedTotal;
            }
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
    std::deque<BridgeEvent> drained;
    std::uint64_t dropped = 0;
    std::uint64_t droppedTotal = 0;
    {
        std::scoped_lock lock(impl_->queueMutex);
        drained.swap(impl_->queue);
        dropped = impl_->droppedSinceLastPoll;
        droppedTotal = impl_->droppedTotal;
        impl_->droppedSinceLastPoll = 0;
    }
    rust::Vec<BridgeEvent> out;
    if (dropped > 0) {
        // Synthetic overflow marker so a consumer knows events were coalesced
        // away since its last poll (u0 = dropped since last poll, u1 = total).
        BridgeEvent overflow{};
        overflow.kind = BridgeEventKind::QueueOverflow;
        overflow.u0 = dropped;
        overflow.u1 = droppedTotal;
        out.push_back(std::move(overflow));
    }
    for (auto& e : drained) {
        out.push_back(std::move(e));
    }
    return out;
}

std::uint64_t BackendBridge::queue_overflow_total() const {
    std::scoped_lock lock(impl_->queueMutex);
    return impl_->droppedTotal;
}

BridgeCommandResult BackendBridge::cancel_operation(std::uint64_t operation_id) {
    try {
        backend::bridge::OperationCommand cmd;
        cmd.action = backend::bridge::OperationCommandAction::Cancel;
        cmd.operationId = operation_id;
        return toBridgeResult(impl_->facade.dispatch(cmd));
    } catch (const std::exception& e) {
        return errorResult(std::string("cancel_operation: ") + e.what());
    } catch (...) {
        return errorResult("cancel_operation: unknown error");
    }
}

BridgeCommandResult BackendBridge::experiment_start(rust::Str output_path) {
    try {
        backend::bridge::ExperimentCommand cmd;
        cmd.action = backend::bridge::ExperimentCommandAction::Start;
        cmd.outputPath = toStd(output_path);
        return toBridgeResult(impl_->facade.dispatch(cmd));
    } catch (const std::exception& e) {
        return errorResult(std::string("experiment_start: ") + e.what());
    } catch (...) {
        return errorResult("experiment_start: unknown error");
    }
}

BridgeCommandResult BackendBridge::experiment_stop() {
    try {
        backend::bridge::ExperimentCommand cmd;
        cmd.action = backend::bridge::ExperimentCommandAction::Stop;
        return toBridgeResult(impl_->facade.dispatch(cmd));
    } catch (const std::exception& e) {
        return errorResult(std::string("experiment_stop: ") + e.what());
    } catch (...) {
        return errorResult("experiment_stop: unknown error");
    }
}

BridgeCommandResult BackendBridge::experiment_cancel() {
    try {
        backend::bridge::ExperimentCommand cmd;
        cmd.action = backend::bridge::ExperimentCommandAction::Cancel;
        return toBridgeResult(impl_->facade.dispatch(cmd));
    } catch (const std::exception& e) {
        return errorResult(std::string("experiment_cancel: ") + e.what());
    } catch (...) {
        return errorResult("experiment_cancel: unknown error");
    }
}

namespace {

BridgeReviewDatasetInfo toDatasetInfo(const backend::bridge::BackendReviewDatasetInfo& info) {
    BridgeReviewDatasetInfo out{};
    out.present = info.present;
    out.count = info.count;
    out.height = info.height;
    out.width = info.width;
    out.channels = info.channels;
    return out;
}

BridgeMonitoringRow toMonitoringRow(const backend::bridge::MonitoringObjectRow& row) {
    BridgeMonitoringRow r{};
    r.frame_index = row.frameIndex;
    r.timestamp_ns = row.timestampNs;
    r.valid = row.valid;
    r.target_group = row.targetGroup;
    r.object_id = row.objectId;
    r.object_count = row.objectCount;
    r.track_id = row.trackId;
    r.centroid_x = row.centroidX;
    r.centroid_y = row.centroidY;
    r.area = row.area;
    r.deformability = row.deformability;
    r.area_ratio = row.areaRatio;
    r.ring_ratio = row.ringRatio;
    r.youngs_modulus = row.youngsModulus;
    return r;
}

} // namespace

BridgeReviewMetadata BackendBridge::fetch_review_metadata() {
    BridgeReviewMetadata out{};
    backend::bridge::BackendReviewMetadata meta;
    if (!impl_->facade.fetchReviewMetadata(meta)) {
        out.valid = false;
        return out;
    }
    out.valid = true;
    out.file_open = meta.fileOpen;
    out.recording_file = meta.recordingFile;
    out.start_time_ns = meta.startTimeNs;
    out.end_time_ns = meta.endTimeNs;
    out.total_valid = meta.totalValid;
    out.total_invalid = meta.totalInvalid;
    out.roi_x = meta.roi.x;
    out.roi_y = meta.roi.y;
    out.roi_w = meta.roi.w;
    out.roi_h = meta.roi.h;
    out.has_background = meta.hasBackground;
    out.has_core_identity = meta.hasCoreIdentity;
    out.core_version = rust::String(meta.coreVersion);
    out.core_source = rust::String(meta.coreSource);
    out.core_release_tag = rust::String(meta.coreReleaseTag);
    out.valid_images = toDatasetInfo(meta.validImages);
    out.invalid_images = toDatasetInfo(meta.invalidImages);
    out.valid_masks = toDatasetInfo(meta.validMasks);
    out.invalid_masks = toDatasetInfo(meta.invalidMasks);
    out.recorded_images = toDatasetInfo(meta.recordedImages);
    out.file_path = rust::String(meta.filePath);
    return out;
}

BridgeReviewMetricsPage BackendBridge::fetch_review_metrics_page(bool valid,
                                                                 std::uint64_t offset,
                                                                 std::uint64_t count) {
    BridgeReviewMetricsPage out{};
    std::vector<backend::bridge::MonitoringObjectRow> rows;
    std::uint64_t total = 0;
    if (!impl_->facade.fetchReviewMetricsPage(valid, offset, count, rows, total)) {
        out.valid = false;
        return out;
    }
    out.valid = true;
    out.total = total;
    out.offset = offset;
    for (const auto& row : rows) {
        out.rows.push_back(toMonitoringRow(row));
    }
    return out;
}

BridgeFrame BackendBridge::fetch_review_image(std::uint32_t dataset, std::uint64_t index) {
    if (dataset > static_cast<std::uint32_t>(backend::bridge::ReviewImageDataset::InvalidMask)) {
        return BridgeFrame{};
    }
    backend::bridge::BackendFrame frame;
    if (!impl_->facade.fetchReviewImage(
            static_cast<backend::bridge::ReviewImageDataset>(dataset), index, frame)) {
        return BridgeFrame{};
    }
    return toBridgeFrame(frame);
}

BridgeCommandResult BackendBridge::review_export_csv(rust::Str output_path) {
    try {
        backend::bridge::ReviewCommand cmd;
        cmd.action = backend::bridge::ReviewCommandAction::ExportMetricsCsv;
        cmd.outputPath = toStd(output_path);
        return toBridgeResult(impl_->facade.dispatch(cmd));
    } catch (const std::exception& e) {
        return errorResult(std::string("review_export_csv: ") + e.what());
    } catch (...) {
        return errorResult("review_export_csv: unknown error");
    }
}

BridgeConfigDocument BackendBridge::fetch_processing_config_json() {
    BridgeConfigDocument out{};
    std::string json;
    if (!impl_->facade.fetchProcessingConfigJson(json)) {
        out.valid = false;
        return out;
    }
    out.valid = true;
    out.json = rust::String(json);
    return out;
}

BridgeCommandResult BackendBridge::apply_processing_config_json(rust::Str json) {
    try {
        backend::bridge::ProcessingSettingsCommand cmd;
        cmd.configJson = toStd(json);
        return toBridgeResult(impl_->facade.dispatch(cmd));
    } catch (const std::exception& e) {
        return errorResult(std::string("apply_processing_config_json: ") + e.what());
    } catch (...) {
        return errorResult("apply_processing_config_json: unknown error");
    }
}

BridgeCommandResult BackendBridge::set_processing_roi(std::int32_t x, std::int32_t y,
                                                      std::int32_t w, std::int32_t h) {
    try {
        backend::bridge::ProcessingSettingsCommand cmd;
        backend::services::ProcessingService::Roi roi;
        roi.x = x;
        roi.y = y;
        roi.w = w;
        roi.h = h;
        cmd.roi = roi;
        return toBridgeResult(impl_->facade.dispatch(cmd));
    } catch (const std::exception& e) {
        return errorResult(std::string("set_processing_roi: ") + e.what());
    } catch (...) {
        return errorResult("set_processing_roi: unknown error");
    }
}

BridgeFrame BackendBridge::fetch_background_image() {
    backend::bridge::BackendFrame frame;
    if (!impl_->facade.fetchBackgroundImage(frame)) {
        return BridgeFrame{};
    }
    return toBridgeFrame(frame);
}

BridgeCommandResult BackendBridge::set_background_image(std::uint64_t width,
                                                        std::uint64_t height,
                                                        rust::Slice<const std::uint8_t> data) {
    try {
        return toBridgeResult(
            impl_->facade.setBackgroundImage(width, height, data.data(), data.size()));
    } catch (const std::exception& e) {
        return errorResult(std::string("set_background_image: ") + e.what());
    } catch (...) {
        return errorResult("set_background_image: unknown error");
    }
}

BridgeCommandResult BackendBridge::clear_background_image() {
    try {
        return toBridgeResult(impl_->facade.clearBackgroundImage());
    } catch (const std::exception& e) {
        return errorResult(std::string("clear_background_image: ") + e.what());
    } catch (...) {
        return errorResult("clear_background_image: unknown error");
    }
}

BridgeProcessingCoreStatus BackendBridge::fetch_processing_core_status() {
    BridgeProcessingCoreStatus out{};
    backend::bridge::BackendProcessingCoreStatus status;
    if (!impl_->facade.fetchProcessingCoreStatus(status)) {
        out.valid = false;
        return out;
    }
    out.valid = true;
    out.active_version = rust::String(status.activeVersion);
    out.contract_version = status.contractVersion;
    out.engine_abi_version = status.engineAbiVersion;
    out.source = rust::String(status.source);
    out.release_tag = rust::String(status.releaseTag);
    out.build_id = rust::String(status.buildId);
    out.artifact_sha256 = rust::String(status.artifactSha256);
    out.required_version = rust::String(status.requiredVersion);
    out.pin_satisfied = status.pinSatisfied;
    return out;
}

BridgeCameraDiscovery BackendBridge::fetch_camera_discovery() {
    BridgeCameraDiscovery out{};
    backend::bridge::BackendCameraDiscovery discovery;
    if (!impl_->facade.fetchCameraDiscovery(discovery)) {
        out.valid = false;
        return out;
    }
    out.valid = true;
    for (const auto& cam : discovery.cameras) {
        BridgeDiscoveredCamera dto{};
        dto.camera_type = static_cast<std::uint32_t>(cam.type);
        dto.camera_index = cam.cameraIndex;
        dto.interface_index = cam.interfaceIndex;
        dto.device_index = cam.deviceIndex;
        dto.interface_id = rust::String(cam.interfaceId);
        dto.device_id = rust::String(cam.deviceId);
        dto.model_name = rust::String(cam.modelName);
        dto.firmware_version = rust::String(cam.firmwareVersion);
        dto.label = rust::String(cam.label);
        out.cameras.push_back(std::move(dto));
    }
    for (const auto& grabber : discovery.framegrabbers) {
        BridgeDiscoveredFramegrabber dto{};
        dto.interface_index = grabber.interfaceIndex;
        dto.device_index = grabber.deviceIndex;
        dto.stream_index = grabber.streamIndex;
        dto.interface_id = rust::String(grabber.interfaceId);
        dto.device_id = rust::String(grabber.deviceId);
        dto.stream_id = rust::String(grabber.streamId);
        dto.model_name = rust::String(grabber.modelName);
        dto.label = rust::String(grabber.label);
        out.framegrabbers.push_back(std::move(dto));
    }
    return out;
}

BridgeCameraSelection BackendBridge::fetch_camera_selection() {
    BridgeCameraSelection out{};
    backend::bridge::BackendCameraSelection selection;
    if (!impl_->facade.fetchCameraSelection(selection)) {
        out.valid = false;
        return out;
    }
    out.valid = true;
    out.mode = static_cast<std::uint32_t>(selection.mode);
    out.interface_index = selection.interfaceIndex;
    out.device_index = selection.deviceIndex;
    out.label = rust::String(selection.label);
    out.mindvision_index = selection.mindVisionIndex;
    out.mindvision_config_path = rust::String(selection.mindVisionConfigPath);
    out.camera_script_path = rust::String(selection.cameraScriptPath);
    out.mock_frame_dir = rust::String(selection.mockFrameDir);
    out.mock_interval_ms = selection.mockIntervalMs;
    out.mock_loop = selection.mockLoop;
    out.configured = selection.configured;
    out.running = selection.running;
    return out;
}

BridgeCommandResult BackendBridge::select_hardware_camera(std::int32_t interface_index,
                                                          std::int32_t device_index,
                                                          rust::Str label) {
    try {
        backend::bridge::CameraCommand cmd;
        cmd.action = backend::bridge::CameraCommandAction::SelectHardwareCamera;
        cmd.hardwareInterfaceIndex = interface_index;
        cmd.hardwareDeviceIndex = device_index;
        cmd.hardwareLabel = toStd(label);
        return toBridgeResult(impl_->facade.dispatch(cmd));
    } catch (const std::exception& e) {
        return errorResult(std::string("select_hardware_camera: ") + e.what());
    } catch (...) {
        return errorResult("select_hardware_camera: unknown error");
    }
}

BridgeCommandResult BackendBridge::select_mindvision_camera(std::int32_t camera_index,
                                                            rust::Str label,
                                                            rust::Str config_path) {
    try {
        backend::bridge::CameraCommand cmd;
        cmd.action = backend::bridge::CameraCommandAction::SelectMindVisionCamera;
        cmd.mindVisionCameraIndex = camera_index;
        cmd.mindVisionLabel = toStd(label);
        cmd.mindVisionConfigPath = toStd(config_path);
        return toBridgeResult(impl_->facade.dispatch(cmd));
    } catch (const std::exception& e) {
        return errorResult(std::string("select_mindvision_camera: ") + e.what());
    } catch (...) {
        return errorResult("select_mindvision_camera: unknown error");
    }
}

BridgeCommandResult BackendBridge::apply_camera_script(rust::Str script_path) {
    try {
        backend::bridge::CameraCommand cmd;
        cmd.action = backend::bridge::CameraCommandAction::ApplyCameraScript;
        cmd.cameraScriptPath = toStd(script_path);
        return toBridgeResult(impl_->facade.dispatch(cmd));
    } catch (const std::exception& e) {
        return errorResult(std::string("apply_camera_script: ") + e.what());
    } catch (...) {
        return errorResult("apply_camera_script: unknown error");
    }
}

BridgeCommandResult BackendBridge::reset_hardware_camera() {
    try {
        backend::bridge::CameraCommand cmd;
        cmd.action = backend::bridge::CameraCommandAction::ResetSelectedHardwareCamera;
        return toBridgeResult(impl_->facade.dispatch(cmd));
    } catch (const std::exception& e) {
        return errorResult(std::string("reset_hardware_camera: ") + e.what());
    } catch (...) {
        return errorResult("reset_hardware_camera: unknown error");
    }
}

BridgeCommandResult BackendBridge::monitoring_set_active(bool active) {
    try {
        backend::bridge::MonitoringCommand cmd;
        cmd.action = active ? backend::bridge::MonitoringCommandAction::Enable
                            : backend::bridge::MonitoringCommandAction::Disable;
        return toBridgeResult(impl_->facade.dispatch(cmd));
    } catch (const std::exception& e) {
        return errorResult(std::string("monitoring_set_active: ") + e.what());
    } catch (...) {
        return errorResult("monitoring_set_active: unknown error");
    }
}

BridgeCommandResult BackendBridge::monitoring_clear() {
    try {
        backend::bridge::MonitoringCommand cmd;
        cmd.action = backend::bridge::MonitoringCommandAction::Clear;
        return toBridgeResult(impl_->facade.dispatch(cmd));
    } catch (const std::exception& e) {
        return errorResult(std::string("monitoring_clear: ") + e.what());
    } catch (...) {
        return errorResult("monitoring_clear: unknown error");
    }
}

BridgeMonitoringSnapshot BackendBridge::fetch_monitoring_snapshot(std::uint64_t max_rows) {
    BridgeMonitoringSnapshot out{};
    backend::bridge::BackendMonitoringSnapshot snapshot;
    if (!impl_->facade.fetchMonitoringSnapshot(snapshot, static_cast<std::size_t>(max_rows))) {
        out.valid = false;
        return out;
    }
    out.valid = true;
    out.monitoring_active = snapshot.monitoringActive;
    out.valid_held = snapshot.validHeld;
    out.invalid_held = snapshot.invalidHeld;
    out.valid_appended = snapshot.validAppended;
    out.invalid_appended = snapshot.invalidAppended;
    out.capacity = snapshot.capacity;
    out.latest_timestamp_ns = snapshot.latestTimestampNs;
    for (const auto& row : snapshot.rows) {
        BridgeMonitoringRow r{};
        r.frame_index = row.frameIndex;
        r.timestamp_ns = row.timestampNs;
        r.valid = row.valid;
        r.target_group = row.targetGroup;
        r.object_id = row.objectId;
        r.object_count = row.objectCount;
        r.track_id = row.trackId;
        r.centroid_x = row.centroidX;
        r.centroid_y = row.centroidY;
        r.area = row.area;
        r.deformability = row.deformability;
        r.area_ratio = row.areaRatio;
        r.ring_ratio = row.ringRatio;
        r.youngs_modulus = row.youngsModulus;
        out.rows.push_back(std::move(r));
    }
    return out;
}

BridgeCommandResult BackendBridge::trigger_set_pulse_duration(std::int32_t pulse_us) {
    try {
        backend::bridge::TriggerCommand cmd;
        cmd.action = backend::bridge::TriggerCommandAction::SetPulseDuration;
        cmd.pulseDurationUs = pulse_us;
        return toBridgeResult(impl_->facade.dispatch(cmd));
    } catch (const std::exception& e) {
        return errorResult(std::string("trigger_set_pulse_duration: ") + e.what());
    } catch (...) {
        return errorResult("trigger_set_pulse_duration: unknown error");
    }
}

BridgeCommandResult BackendBridge::trigger_manual_pulse() {
    try {
        backend::bridge::TriggerCommand cmd;
        cmd.action = backend::bridge::TriggerCommandAction::ManualPulse;
        return toBridgeResult(impl_->facade.dispatch(cmd));
    } catch (const std::exception& e) {
        return errorResult(std::string("trigger_manual_pulse: ") + e.what());
    } catch (...) {
        return errorResult("trigger_manual_pulse: unknown error");
    }
}

BridgeCommandResult BackendBridge::trigger_periodic_start(std::int32_t interval_ms) {
    try {
        backend::bridge::TriggerCommand cmd;
        cmd.action = backend::bridge::TriggerCommandAction::StartPeriodicTest;
        cmd.periodicIntervalMs = interval_ms;
        return toBridgeResult(impl_->facade.dispatch(cmd));
    } catch (const std::exception& e) {
        return errorResult(std::string("trigger_periodic_start: ") + e.what());
    } catch (...) {
        return errorResult("trigger_periodic_start: unknown error");
    }
}

BridgeCommandResult BackendBridge::trigger_periodic_stop() {
    try {
        backend::bridge::TriggerCommand cmd;
        cmd.action = backend::bridge::TriggerCommandAction::StopPeriodicTest;
        return toBridgeResult(impl_->facade.dispatch(cmd));
    } catch (const std::exception& e) {
        return errorResult(std::string("trigger_periodic_stop: ") + e.what());
    } catch (...) {
        return errorResult("trigger_periodic_stop: unknown error");
    }
}

BridgeTriggerStatus BackendBridge::fetch_trigger_status() {
    BridgeTriggerStatus out{};
    backend::bridge::BackendTriggerStatus status;
    if (!impl_->facade.fetchTriggerStatus(status)) {
        out.valid = false;
        return out;
    }
    out.valid = true;
    out.camera_attached = status.cameraAttached;
    out.pulse_duration_us = status.pulseDurationUs;
    out.trigger_count = status.triggerCount;
    out.last_onset_us = status.lastOnsetUs;
    out.last_object_id = status.lastObjectId;
    out.last_track_id = status.lastTrackId;
    out.periodic_active = status.periodicActive;
    out.periodic_interval_ms = status.periodicIntervalMs;
    return out;
}

BridgeExperimentStatus BackendBridge::fetch_experiment_status() {
    BridgeExperimentStatus out{};
    backend::ExperimentCoordinator::Status status;
    if (!impl_->facade.fetchExperimentStatus(status)) {
        out.valid = false;
        return out;
    }
    out.valid = true;
    out.state = static_cast<std::uint32_t>(status.state);
    out.start_time_ns = status.startTimeNs;
    out.end_time_ns = status.endTimeNs;
    out.valid_buffered = status.validBuffered;
    out.invalid_buffered = status.invalidBuffered;
    out.valid_saved = status.validSaved;
    out.invalid_saved = status.invalidSaved;
    out.dropped_valid = status.droppedValid;
    out.dropped_invalid = status.droppedInvalid;
    out.flushing = status.flushing;
    out.cancelled = status.cancelled;
    out.output_path = rust::String(status.outputPath);
    out.message = rust::String(status.message);
    return out;
}

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
// processing commands (apply_processing, fetch_processing_stats); v4 added
// operation state (OperationStatus events, cancel_operation, result
// operation_id), the bounded event queue with QueueOverflow, and the extended
// error sources; v5 added the experiment lifecycle (experiment_start/stop/
// cancel, fetch_experiment_status, ExperimentStatus events); v6 added the
// bounded monitoring snapshot and sorter trigger commands/status (BE-5); v7
// added camera discovery/selection (fetch_camera_discovery/selection,
// select_hardware/mindvision_camera, apply_camera_script,
// reset_hardware_camera — BE-2); v8 added the processing config document
// round-trip, ROI/background binary transfer, and processing-core status
// (BE-3); v9 added paged HDF5 review (metadata, metrics pages, image/mask
// pulls, cancellable CSV export jobs — BE-6). All additive over v1
// (ADR 0003/0004). Must match contract/bridge-contract.json.
std::uint32_t bridge_abi_version() { return 9; }

} // namespace mib_bridge

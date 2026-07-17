#include "backend/diagnostics/PipelineTimingRecorder.h"

#include "backend/app/Tools.h"

#include <filesystem>
#include <fstream>
#include <system_error>

namespace backend::diagnostics {

PipelineTimingRecorder::PipelineTimingRecorder()
    : frames_(kFrameCapacity), triggers_(kTriggerCapacity) {}

PipelineTimingRecorder& PipelineTimingRecorder::instance() {
    static PipelineTimingRecorder recorder;
    return recorder;
}

uint64_t PipelineTimingRecorder::nowUs() {
    return backend::Tools::getTimestamp();
}

void PipelineTimingRecorder::recordFrame(const FrameTimingRecord& record) {
    if (!isEnabled()) return;
    const uint64_t count = frameCount_.load(std::memory_order_relaxed);
    frames_[static_cast<size_t>(count % kFrameCapacity)] = record;
    frameCount_.store(count + 1, std::memory_order_release);
}

void PipelineTimingRecorder::recordTrigger(const TriggerTimingRecord& record) {
    if (!isEnabled()) return;
    const uint64_t count = triggerCount_.load(std::memory_order_relaxed);
    triggers_[static_cast<size_t>(count % kTriggerCapacity)] = record;
    triggerCount_.store(count + 1, std::memory_order_release);
}

void PipelineTimingRecorder::countSkipped(PipelineSkipReason reason, uint64_t n) {
    if (!isEnabled()) return;
    skipped_[static_cast<size_t>(reason)].fetch_add(n, std::memory_order_relaxed);
}

void PipelineTimingRecorder::clear() {
    frameCount_.store(0, std::memory_order_release);
    triggerCount_.store(0, std::memory_order_release);
    for (auto& counter : skipped_) {
        counter.store(0, std::memory_order_relaxed);
    }
}

namespace {

// Copy the last min(count, capacity) records, oldest first.
template <typename Record>
std::vector<Record> snapshotRing(const std::vector<Record>& ring, uint64_t count, size_t capacity) {
    const uint64_t retained = count < capacity ? count : capacity;
    std::vector<Record> out;
    out.reserve(static_cast<size_t>(retained));
    for (uint64_t i = count - retained; i < count; ++i) {
        out.push_back(ring[static_cast<size_t>(i % capacity)]);
    }
    return out;
}

} // namespace

std::vector<FrameTimingRecord> PipelineTimingRecorder::frameRecords() const {
    return snapshotRing(frames_, frameCount_.load(std::memory_order_acquire), kFrameCapacity);
}

std::vector<TriggerTimingRecord> PipelineTimingRecorder::triggerRecords() const {
    return snapshotRing(triggers_, triggerCount_.load(std::memory_order_acquire), kTriggerCapacity);
}

const char* pipelineSkipReasonName(PipelineSkipReason reason) {
    switch (reason) {
    case PipelineSkipReason::DroppedToLatest:
        return "dropped_to_latest";
    case PipelineSkipReason::RingBehind:
        return "ring_behind";
    case PipelineSkipReason::EmptyFrame:
        return "empty_frame";
    case PipelineSkipReason::KernelError:
        return "kernel_error";
    case PipelineSkipReason::BatchQueueRejected:
        return "batch_queue_rejected";
    case PipelineSkipReason::Count:
        break;
    }
    return "unknown";
}

bool PipelineTimingRecorder::dumpCsv(const std::string& directory, std::string* error) const {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(directory, ec);
    if (ec) {
        if (error) *error = "create_directories failed: " + ec.message();
        return false;
    }

    auto fail = [&](const std::string& what) {
        if (error) *error = "failed to write " + what;
        return false;
    };

    {
        std::ofstream out(fs::path(directory) / "pipeline_frames.csv", std::ios::trunc);
        if (!out) return fail("pipeline_frames.csv");
        out << "frame_index,device_timestamp,grab_us,algo_start_us,algo_end_us,"
               "trigger_dispatch_us,callbacks_done_us,valid_count,invalid_count,"
               "is_target_group\n";
        for (const auto& r : frameRecords()) {
            out << r.frameIndex << ',' << r.deviceTimestamp << ',' << r.grabUs << ','
                << r.algoStartUs << ',' << r.algoEndUs << ',' << r.triggerDispatchUs << ','
                << r.callbacksDoneUs << ',' << r.validCount << ',' << r.invalidCount << ','
                << static_cast<unsigned>(r.isTargetGroup) << '\n';
        }
        if (!out.good()) return fail("pipeline_frames.csv");
    }

    {
        std::ofstream out(fs::path(directory) / "pipeline_triggers.csv", std::ios::trunc);
        if (!out) return fail("pipeline_triggers.csv");
        out << "frame_index,grab_us,request_us,wake_us,fire_us,pulse_done_us,coalesced\n";
        for (const auto& r : triggerRecords()) {
            out << r.frameIndex << ',' << r.grabUs << ',' << r.requestUs << ',' << r.wakeUs << ','
                << r.fireUs << ',' << r.pulseDoneUs << ',' << r.coalesced << '\n';
        }
        if (!out.good()) return fail("pipeline_triggers.csv");
    }

    {
        std::ofstream out(fs::path(directory) / "pipeline_skips.csv", std::ios::trunc);
        if (!out) return fail("pipeline_skips.csv");
        out << "reason,count\n";
        for (size_t i = 0; i < static_cast<size_t>(PipelineSkipReason::Count); ++i) {
            out << pipelineSkipReasonName(static_cast<PipelineSkipReason>(i)) << ','
                << skipped_[i].load(std::memory_order_relaxed) << '\n';
        }
        out << "frame_records," << frameRecordCount() << '\n';
        out << "trigger_records," << triggerRecordCount() << '\n';
        if (!out.good()) return fail("pipeline_skips.csv");
    }

    return true;
}

} // namespace backend::diagnostics

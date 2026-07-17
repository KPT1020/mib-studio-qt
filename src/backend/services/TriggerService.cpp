#include "backend/services/TriggerService.h"
#include "backend/camera/common/ICamera.h"
#include "backend/diagnostics/PipelineTimingRecorder.h"
#include <spdlog/spdlog.h>
#include <chrono>

namespace backend::services {

TriggerService::TriggerService() = default;

TriggerService::~TriggerService() { stop(); }

void TriggerService::start() {
    if (running_.load()) return;
    running_.store(true);
    thread_ = std::thread(&TriggerService::triggerLoop, this);
    SPDLOG_INFO("TriggerService started");
}

void TriggerService::stop() {
    if (!running_.load()) return;
    // Clear running_ while holding triggerMutex_ (the mutex the trigger thread
    // holds when evaluating its wait predicate) before notifying. Storing it
    // lock-free races with the loop's wait(): if the thread has just been
    // started and checks the predicate (running_ == true) but has not yet
    // blocked, a lock-free store+notify here lands in that gap and is lost, so
    // the thread blocks forever and this join() deadlocks. Under rapid
    // start/stop (e.g. capture restart) that hangs capture shutdown. Taking the
    // lock closes the window.
    {
        std::lock_guard<std::mutex> lk(triggerMutex_);
        running_.store(false);
    }
    triggerCV_.notify_all();
    if (thread_.joinable()) thread_.join();
    SPDLOG_INFO("TriggerService stopped");
}

void TriggerService::setCamera(camera::common::ICamera* camera) {
    camera_.store(camera, std::memory_order_release);
    if (camera) {
        camera->configureTriggerOutput("TTLIO12");
    }
}

void TriggerService::onTargetGroupResult(const TargetGroupSignal& signal) {
    if (!signal.isTargetGroup) {
        return;
    }

    SPDLOG_DEBUG("TriggerService target-group callback: objectId={}, trackId={}", signal.objectId, signal.trackId);

    lastTriggerObjectId_.store(signal.objectId, std::memory_order_release);
    lastTriggerTrackId_.store(signal.trackId, std::memory_order_release);
    const bool recordTiming = backend::diagnostics::PipelineTimingRecorder::instance().isEnabled();
    const uint64_t requestUs =
        recordTiming ? backend::diagnostics::PipelineTimingRecorder::nowUs() : 0;
    // Set the request flag while holding the same mutex the trigger thread uses
    // for its wait() predicate. Storing it lock-free races with the consumer's
    // predicate check: if the flag is set after the consumer evaluates the
    // predicate but before it blocks, the notify is lost and the trigger is
    // delayed until the next request (or dropped entirely under bursts). Taking
    // the lock closes that window.
    {
        std::lock_guard<std::mutex> lk(triggerMutex_);
        if (triggerRequested_.load(std::memory_order_relaxed)) {
            // A pulse request is already pending: this request coalesces into
            // it (single-bool limitation). Keep the pending identity — its
            // request has waited longest — and count the merge.
            ++pendingRequest_.coalesced;
        } else {
            pendingRequest_ =
                PendingRequest{signal.frameIndex, signal.hostTimestampUs, requestUs, 0};
        }
        triggerRequested_.store(true, std::memory_order_release);
    }
    triggerCV_.notify_one();
}

void TriggerService::triggerLoop() {
    auto& timingRecorder = backend::diagnostics::PipelineTimingRecorder::instance();
    while (running_.load()) {
        PendingRequest pending;
        {
            std::unique_lock<std::mutex> lk(triggerMutex_);
            triggerCV_.wait(lk, [this] {
                return !running_.load() || triggerRequested_.load(std::memory_order_acquire);
            });
            pending = pendingRequest_;
        }
        if (!running_.load()) break;

        if (triggerRequested_.exchange(false, std::memory_order_acq_rel)) {
            auto* cam = camera_.load(std::memory_order_acquire);
            if (!cam) continue;

            const bool recordTiming = timingRecorder.isEnabled();
            const uint64_t wakeUs =
                recordTiming ? backend::diagnostics::PipelineTimingRecorder::nowUs() : 0;

            // Fire trigger pulse: High -> busy-wait ~1us -> Low
            // Mirrors processTrigger() in MIB-Studio/src/mib_grabber/mib_grabber.cpp
            auto start = std::chrono::high_resolution_clock::now();
            if (!cam->setTriggerOutput(true)) continue;
            auto onset = std::chrono::high_resolution_clock::now();
            const uint64_t fireUs =
                recordTiming ? backend::diagnostics::PipelineTimingRecorder::nowUs() : 0;

            // Busy-wait for the configured pulse duration
            auto pulseUs = std::chrono::microseconds(pulseDurationUs_.load(std::memory_order_relaxed));
            while (running_.load() &&
                   std::chrono::high_resolution_clock::now() - onset < pulseUs) {
                // Busy-wait
            }

            cam->setTriggerOutput(false);

            // Record metrics
            auto onsetUs = std::chrono::duration<double, std::micro>(onset - start).count();
            lastOnsetUs_.store(onsetUs, std::memory_order_relaxed);
            triggerCount_.fetch_add(1, std::memory_order_relaxed);

            if (recordTiming) {
                backend::diagnostics::TriggerTimingRecord record;
                record.frameIndex = pending.frameIndex;
                record.grabUs = pending.hostTimestampUs;
                record.requestUs = pending.requestUs;
                record.wakeUs = wakeUs;
                record.fireUs = fireUs;
                record.pulseDoneUs = backend::diagnostics::PipelineTimingRecorder::nowUs();
                record.coalesced = pending.coalesced;
                timingRecorder.recordTrigger(record);
            }
        }
    }
}

} // namespace backend::services

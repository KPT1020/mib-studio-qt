#include "backend/services/TriggerService.h"
#include "backend/camera/common/ICamera.h"
#include <spdlog/spdlog.h>
#include <chrono>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#endif

namespace backend::services {

namespace {

// The trigger thread's wake-up latency and busy-wait pulse timing are directly
// visible on the TTL line (LED/sort pulse onset + width). Elevate its
// scheduling priority so background load (e.g. the HDF5 writer flushing an
// experiment batch) cannot preempt a pending pulse. Best-effort: failure is
// logged and the thread runs at default priority.
void raiseTriggerThreadPriority() {
#ifdef _WIN32
    if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL)) {
        SPDLOG_WARN("TriggerService: failed to raise trigger thread priority (error={})",
                    GetLastError());
    } else {
        SPDLOG_INFO("TriggerService: trigger thread priority set to TIME_CRITICAL");
    }
#else
    sched_param sp{};
    sp.sched_priority = sched_get_priority_min(SCHED_FIFO);
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0) {
        // Real-time scheduling normally needs elevated privileges on Linux;
        // dev/CI runs fall back to default priority.
        SPDLOG_DEBUG("TriggerService: real-time priority unavailable, using default");
    } else {
        SPDLOG_INFO("TriggerService: trigger thread scheduled SCHED_FIFO");
    }
#endif
}

} // namespace

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
    // Set the request flag while holding the same mutex the trigger thread uses
    // for its wait() predicate. Storing it lock-free races with the consumer's
    // predicate check: if the flag is set after the consumer evaluates the
    // predicate but before it blocks, the notify is lost and the trigger is
    // delayed until the next request (or dropped entirely under bursts). Taking
    // the lock closes that window.
    {
        std::lock_guard<std::mutex> lk(triggerMutex_);
        triggerRequested_.store(true, std::memory_order_release);
    }
    triggerCV_.notify_one();
}

void TriggerService::triggerLoop() {
    raiseTriggerThreadPriority();
    while (running_.load()) {
        {
            std::unique_lock<std::mutex> lk(triggerMutex_);
            triggerCV_.wait(lk, [this] {
                return !running_.load() || triggerRequested_.load(std::memory_order_acquire);
            });
        }
        if (!running_.load()) break;

        if (triggerRequested_.exchange(false, std::memory_order_acq_rel)) {
            auto* cam = camera_.load(std::memory_order_acquire);
            if (!cam) continue;

            // Fire trigger pulse: High -> busy-wait ~1us -> Low
            // Mirrors processTrigger() in MIB-Studio/src/mib_grabber/mib_grabber.cpp
            auto start = std::chrono::high_resolution_clock::now();
            if (!cam->setTriggerOutput(true)) continue;
            auto onset = std::chrono::high_resolution_clock::now();

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
        }
    }
}

} // namespace backend::services

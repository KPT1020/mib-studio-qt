#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace camera::common { class ICamera; }

namespace backend::services {

class TriggerService {
public:
    TriggerService();
    ~TriggerService();

    void start();
    void stop();

    // Set the camera to use for trigger output (called when camera becomes available)
    void setCamera(camera::common::ICamera* camera);

    // Called by ProcessingService callback when a valid frame is classified.
    // captureObserved is the steady_clock time at which the frame was captured
    // (recorded in CaptureService immediately after grabFrame returns). The
    // trigger pulse onset is scheduled for captureObserved + triggerDelayUs,
    // decoupling onset from variable processing-pipeline latency.
    void onTargetGroupResult(bool isTargetGroup,
                             std::chrono::steady_clock::time_point captureObserved);

    // Pulse duration (microseconds)
    void setPulseDurationUs(int us) { pulseDurationUs_.store(us, std::memory_order_relaxed); }
    int getPulseDurationUs() const { return pulseDurationUs_.load(std::memory_order_relaxed); }

    // Fixed delay from frame capture to trigger onset (microseconds).
    // 0 preserves legacy "fire as soon as possible after classification" behavior.
    void setTriggerDelayUs(int us) { triggerDelayUs_.store(us, std::memory_order_relaxed); }
    int getTriggerDelayUs() const { return triggerDelayUs_.load(std::memory_order_relaxed); }

    // Metrics
    uint64_t getTriggerCount() const { return triggerCount_.load(std::memory_order_relaxed); }
    // Onset latency of the last pulse, measured from request wake to setTriggerOutput(true)
    // returning. Legacy metric retained for backwards compatibility.
    double getLastOnsetUs() const { return lastOnsetUs_.load(std::memory_order_relaxed); }
    // Realized delay of the last pulse: time from captureObserved to actual pulse onset.
    // Ideally equals getTriggerDelayUs(); drift indicates scheduler jitter.
    double getLastRealizedDelayUs() const { return lastRealizedDelayUs_.load(std::memory_order_relaxed); }
    // Last slip: how much the scheduled deadline was missed by (0 if fired on time).
    double getLastSlipUs() const { return lastSlipUs_.load(std::memory_order_relaxed); }
    // Number of triggers dropped because the pending-deadline queue was full.
    uint64_t getDroppedTriggers() const { return droppedTriggers_.load(std::memory_order_relaxed); }

    void resetMetrics() {
        triggerCount_.store(0, std::memory_order_relaxed);
        lastOnsetUs_.store(0.0, std::memory_order_relaxed);
        lastRealizedDelayUs_.store(0.0, std::memory_order_relaxed);
        lastSlipUs_.store(0.0, std::memory_order_relaxed);
        droppedTriggers_.store(0, std::memory_order_relaxed);
    }

private:
    void triggerLoop();

    // A pending trigger request: remember both the capture instant (for realized-delay
    // metric) and the absolute deadline at which the pulse should fire.
    struct PendingTrigger {
        std::chrono::steady_clock::time_point captureObserved;
        std::chrono::steady_clock::time_point deadline;
    };

    // Bound the pending queue to guard against runaway growth if a long delay is
    // configured and target-group hits arrive faster than they can be emitted.
    static constexpr size_t kMaxPendingTriggers = 32;

    std::thread thread_;
    std::atomic<bool> running_{false};

    // Trigger request signaling: queue of pending (captureObserved, deadline) pairs.
    // Entries are monotonically non-decreasing in deadline because capture time is
    // monotonic and delay is uniform, so front() is always the earliest fire time.
    std::mutex triggerMutex_;
    std::condition_variable triggerCV_;
    std::deque<PendingTrigger> pendingTriggers_;

    // Camera reference (non-owning)
    std::atomic<camera::common::ICamera*> camera_{nullptr};

    // Pulse duration
    std::atomic<int> pulseDurationUs_{1};
    // Fixed delay from frame capture to trigger onset (0 == fire ASAP).
    std::atomic<int> triggerDelayUs_{0};

    // Metrics
    std::atomic<uint64_t> triggerCount_{0};
    std::atomic<double> lastOnsetUs_{0.0};
    std::atomic<double> lastRealizedDelayUs_{0.0};
    std::atomic<double> lastSlipUs_{0.0};
    std::atomic<uint64_t> droppedTriggers_{0};
};

} // namespace backend::services

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace camera::common {
class ICamera;
}

namespace backend::services {

struct TargetGroupSignal {
    bool isTargetGroup{false};
    int objectId{-1};
    int trackId{-1};
    // Source-frame identity for end-to-end latency correlation (see
    // PipelineTimingRecorder): FrameStore write index and host monotonic
    // acquisition stamp (microseconds; 0 if unknown).
    uint64_t frameIndex{0};
    uint64_t hostTimestampUs{0};
};

class TriggerService {
public:
    TriggerService();
    ~TriggerService();

    void start();
    void stop();

    // Set the camera to use for trigger output (called when camera becomes available)
    void setCamera(camera::common::ICamera* camera);

    // Called by ProcessingService callback when a frame has target-group ownership.
    // Metadata carries trigger owner identity; one trigger request is expected
    // per owning source frame.
    void onTargetGroupResult(const TargetGroupSignal& signal);

    // Pulse duration (microseconds)
    void setPulseDurationUs(int us) { pulseDurationUs_.store(us, std::memory_order_relaxed); }
    int getPulseDurationUs() const { return pulseDurationUs_.load(std::memory_order_relaxed); }

    // Metrics
    uint64_t getTriggerCount() const { return triggerCount_.load(std::memory_order_relaxed); }
    double getLastOnsetUs() const { return lastOnsetUs_.load(std::memory_order_relaxed); }
    int getLastTriggerObjectId() const {
        return lastTriggerObjectId_.load(std::memory_order_relaxed);
    }
    int getLastTriggerTrackId() const {
        return lastTriggerTrackId_.load(std::memory_order_relaxed);
    }
    // Requests evicted from a full pending queue (oldest dropped first). A
    // non-zero value means target-group frames arrived faster than pulses
    // could be driven for longer than the queue could absorb (issue #283).
    uint64_t getDroppedRequestCount() const {
        return droppedRequests_.load(std::memory_order_relaxed);
    }

    void resetMetrics() {
        triggerCount_.store(0, std::memory_order_relaxed);
        lastOnsetUs_.store(0.0, std::memory_order_relaxed);
        lastTriggerObjectId_.store(-1, std::memory_order_relaxed);
        lastTriggerTrackId_.store(-1, std::memory_order_relaxed);
        droppedRequests_.store(0, std::memory_order_relaxed);
    }

    // Bound on the pending-request queue. Sized to absorb a realistic burst
    // of same-window target frames; beyond it the OLDEST request is dropped
    // (and counted) — a backlog of stale pulses is worse than a counted drop.
    static constexpr size_t kMaxPendingRequests = 8;

private:
    void triggerLoop();

    std::thread thread_;
    std::atomic<bool> running_{false};

    // Trigger request signaling
    std::mutex triggerMutex_;
    std::condition_variable triggerCV_;

    // Per-request metadata for pulses and latency instrumentation
    // (PipelineTimingRecorder). Every target-group request gets its own entry
    // — and therefore its own pulse, in arrival order — instead of the old
    // single-bool flag that silently coalesced requests arriving while the
    // trigger thread was mid-pulse (issue #283). Bounded by
    // kMaxPendingRequests with counted drop-oldest overflow. Accessed only
    // under triggerMutex_.
    struct PendingRequest {
        uint64_t frameIndex{0};
        uint64_t hostTimestampUs{0};
        uint64_t requestUs{0};
    };
    std::deque<PendingRequest> pendingRequests_;

    // Camera reference (non-owning)
    std::atomic<camera::common::ICamera*> camera_{nullptr};

    // Pulse duration
    std::atomic<int> pulseDurationUs_{1};
    std::atomic<int> lastTriggerObjectId_{-1};
    std::atomic<int> lastTriggerTrackId_{-1};

    // Metrics
    std::atomic<uint64_t> triggerCount_{0};
    std::atomic<double> lastOnsetUs_{0.0};
    std::atomic<uint64_t> droppedRequests_{0};
};

} // namespace backend::services

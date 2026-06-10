#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace camera::common { class ICamera; }

namespace backend::services {

struct TargetGroupSignal {
    bool isTargetGroup{false};
    int objectId{-1};
    int trackId{-1};
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
    int getLastTriggerObjectId() const { return lastTriggerObjectId_.load(std::memory_order_relaxed); }
    int getLastTriggerTrackId() const { return lastTriggerTrackId_.load(std::memory_order_relaxed); }

    void resetMetrics() {
        triggerCount_.store(0, std::memory_order_relaxed);
        lastOnsetUs_.store(0.0, std::memory_order_relaxed);
        lastTriggerObjectId_.store(-1, std::memory_order_relaxed);
        lastTriggerTrackId_.store(-1, std::memory_order_relaxed);
    }

private:
    void triggerLoop();

    std::thread thread_;
    std::atomic<bool> running_{false};

    // Trigger request signaling
    std::mutex triggerMutex_;
    std::condition_variable triggerCV_;
    std::atomic<bool> triggerRequested_{false};

    // Camera reference (non-owning)
    std::atomic<camera::common::ICamera*> camera_{nullptr};

    // Pulse duration
    std::atomic<int> pulseDurationUs_{1};
    std::atomic<int> lastTriggerObjectId_{-1};
    std::atomic<int> lastTriggerTrackId_{-1};

    // Metrics
    std::atomic<uint64_t> triggerCount_{0};
    std::atomic<double> lastOnsetUs_{0.0};
};

} // namespace backend::services

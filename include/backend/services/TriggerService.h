#pragma once

#include <atomic>
#include <condition_variable>
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

    // Called by ProcessingService callback when a valid frame is classified
    void onTargetGroupResult(bool isTargetGroup);

    // Metrics
    uint64_t getTriggerCount() const { return triggerCount_.load(std::memory_order_relaxed); }
    double getLastOnsetUs() const { return lastOnsetUs_.load(std::memory_order_relaxed); }

    void resetMetrics() {
        triggerCount_.store(0, std::memory_order_relaxed);
        lastOnsetUs_.store(0.0, std::memory_order_relaxed);
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

    // Metrics
    std::atomic<uint64_t> triggerCount_{0};
    std::atomic<double> lastOnsetUs_{0.0};
};

} // namespace backend::services

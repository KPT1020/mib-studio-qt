#include "backend/services/TriggerService.h"
#include "camera/common/ICamera.h"
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
    running_.store(false);
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

void TriggerService::onTargetGroupResult(bool isTargetGroup,
                                         std::chrono::steady_clock::time_point captureObserved) {
    if (!isTargetGroup) return;

    const auto delayUs = std::chrono::microseconds(
        triggerDelayUs_.load(std::memory_order_relaxed));
    PendingTrigger pt;
    pt.captureObserved = captureObserved;
    pt.deadline = captureObserved + delayUs;

    {
        std::scoped_lock lk(triggerMutex_);
        if (pendingTriggers_.size() >= kMaxPendingTriggers) {
            // Drop the oldest pending request. A full queue implies the trigger
            // thread cannot keep up (pulse duration + scheduling overhead > inter-hit
            // interval), so preserving the most recent events is more useful than
            // the oldest.
            pendingTriggers_.pop_front();
            droppedTriggers_.fetch_add(1, std::memory_order_relaxed);
            SPDLOG_WARN("TriggerService: pending queue full ({}), dropped oldest trigger",
                        kMaxPendingTriggers);
        }
        pendingTriggers_.push_back(pt);
    }
    triggerCV_.notify_one();
}

void TriggerService::triggerLoop() {
    while (running_.load()) {
        PendingTrigger next{};
        {
            std::unique_lock<std::mutex> lk(triggerMutex_);
            // Wait until either shutdown or a request arrives.
            triggerCV_.wait(lk, [this] {
                return !running_.load() || !pendingTriggers_.empty();
            });
            if (!running_.load()) break;

            // Wait until the front deadline arrives (or a new earlier one is pushed,
            // or shutdown). Because capture time is monotonic and delay is uniform,
            // the front is always the earliest deadline.
            auto deadline = pendingTriggers_.front().deadline;
            triggerCV_.wait_until(lk, deadline, [this, deadline] {
                return !running_.load()
                    || pendingTriggers_.empty()
                    || pendingTriggers_.front().deadline < deadline
                    || std::chrono::steady_clock::now() >= deadline;
            });
            if (!running_.load()) break;
            if (pendingTriggers_.empty()) continue;

            const auto now = std::chrono::steady_clock::now();
            if (now < pendingTriggers_.front().deadline) {
                // Front deadline was superseded by an earlier one (shouldn't happen
                // given monotonic capture), or spurious wake. Re-loop.
                continue;
            }

            next = pendingTriggers_.front();
            pendingTriggers_.pop_front();
        }

        auto* cam = camera_.load(std::memory_order_acquire);
        if (!cam) continue;

        // Compute slip (how much we missed the deadline by) before firing.
        const auto preFire = std::chrono::high_resolution_clock::now();
        const auto slip = std::chrono::steady_clock::now() - next.deadline;
        const double slipUs = std::chrono::duration<double, std::micro>(slip).count();
        if (slipUs > 0.0) {
            // Processing (or scheduling) exceeded the configured delay budget.
            lastSlipUs_.store(slipUs, std::memory_order_relaxed);
            SPDLOG_WARN("TriggerService: slip {:.1f} us (delay budget {} us exceeded; firing immediately)",
                        slipUs, triggerDelayUs_.load(std::memory_order_relaxed));
        } else {
            lastSlipUs_.store(0.0, std::memory_order_relaxed);
        }

        // Fire the pulse: high -> busy-wait pulseDurationUs -> low.
        if (!cam->setTriggerOutput(true)) continue;
        const auto onset = std::chrono::high_resolution_clock::now();

        const auto pulseUs = std::chrono::microseconds(
            pulseDurationUs_.load(std::memory_order_relaxed));
        while (running_.load() &&
               std::chrono::high_resolution_clock::now() - onset < pulseUs) {
            // Busy-wait for precise pulse width.
        }

        cam->setTriggerOutput(false);

        // Record metrics.
        const double onsetUs = std::chrono::duration<double, std::micro>(onset - preFire).count();
        lastOnsetUs_.store(onsetUs, std::memory_order_relaxed);

        const auto realized = onset - next.captureObserved;
        const double realizedUs = std::chrono::duration<double, std::micro>(realized).count();
        lastRealizedDelayUs_.store(realizedUs, std::memory_order_relaxed);

        triggerCount_.fetch_add(1, std::memory_order_relaxed);
    }
}

} // namespace backend::services

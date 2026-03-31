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

void TriggerService::onTargetGroupResult(bool isTargetGroup) {
    if (isTargetGroup) {
        triggerRequested_.store(true, std::memory_order_release);
        triggerCV_.notify_one();
    }
}

void TriggerService::triggerLoop() {
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

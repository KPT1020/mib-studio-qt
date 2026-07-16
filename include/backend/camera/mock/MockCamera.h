#pragma once

#include "backend/camera/common/ICamera.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace camera::mock {

struct MockCameraOptions {
    std::filesystem::path folder;
    std::chrono::microseconds frameInterval{33'000};
    bool loopFiles{true};
};

class MockCamera : public camera::common::ICamera {
public:
    explicit MockCamera(MockCameraOptions options);
    ~MockCamera() override = default;

    void applyConfig(const camera::common::CameraConfig& config) override;
    bool start() override;
    void stop() override;
    bool isRunning() const override { return running_.load(std::memory_order_acquire); }

    bool grabFrame(camera::common::Frame& out) override;
    bool pollStats(camera::common::CameraStats& out) const override;

    void setFrameInterval(std::chrono::microseconds interval);
    void setLooping(bool loop);

    // Trigger-output emulation (BE-5): the mock camera accepts trigger pulses
    // so the sorter trigger chain (manual/periodic test) is headless-testable.
    // Real hardware drives an actual output line; here we just latch state.
    bool setTriggerOutput(bool high) override {
        triggerLineHigh_.store(high, std::memory_order_relaxed);
        if (high) {
            triggerPulses_.fetch_add(1, std::memory_order_relaxed);
        }
        return true;
    }
    std::uint64_t triggerPulseCount() const {
        return triggerPulses_.load(std::memory_order_relaxed);
    }

private:
    void refreshFileList();
    bool loadFrameFromPath(const std::filesystem::path& path, camera::common::Frame& frame);
    bool preloadFrames();

    MockCameraOptions options_;
    camera::common::CameraConfig config_{};
    std::vector<std::filesystem::path> files_;
    std::vector<camera::common::Frame> preloadedFrames_;
    size_t nextIndex_{0};

    std::atomic<bool> running_{false};
    std::chrono::steady_clock::time_point lastFrameTime_{};
    mutable camera::common::CameraStats stats_{};

    // Trigger-output emulation state (BE-5)
    std::atomic<bool> triggerLineHigh_{false};
    std::atomic<std::uint64_t> triggerPulses_{0};
};

} // namespace camera::mock



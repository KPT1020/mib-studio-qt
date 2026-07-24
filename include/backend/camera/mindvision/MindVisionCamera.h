#pragma once

#include "backend/camera/common/ICamera.h"
#include "backend/camera/common/WindowedRate.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

namespace camera::common
{

class MindVisionCamera : public ICamera
{
public:
    explicit MindVisionCamera(int cameraIndex, std::string configPath = {});
    ~MindVisionCamera() override;

    void applyConfig(const CameraConfig &config) override;
    bool start() override;
    void stop() override;
    bool isRunning() const override { return running_.load(std::memory_order_acquire); }

    bool grabFrame(Frame &out) override;
    bool pollStats(CameraStats &out) const override;
    bool checkDeviceHealth() const override;

    void configureTriggerOutput(const std::string &lineSelector) override;
    bool setTriggerOutput(bool high) override;

private:
    bool applyJsonConfig(int hCamera);

    int cameraIndex_{-1};
    std::string configPath_;
    CameraConfig config_{};

    int hCamera_{-1};
    uint8_t *outBuffer_{nullptr};
    int bufferWidth_{0};
    int bufferHeight_{0};

    std::atomic<bool> running_{false};
    mutable std::mutex stateMutex_;

    // The trigger path (TriggerService's pulse thread) must never contend with
    // stateMutex_: grabFrame holds it across CameraImageProcess and stop()
    // holds it across teardown, either of which would add milliseconds of
    // jitter to a pulse edge. triggerMutex_ guards only what the pulse needs.
    mutable std::mutex triggerMutex_;
    int triggerCameraHandle_{-1}; // guarded by triggerMutex_; -1 once stop() begins teardown
    int triggerOutputIndex_{-1};  // guarded by triggerMutex_
    // GPIO output requested by the JSON config (trigger_output_index); applied
    // to triggerOutputIndex_ when TriggerService configures the output.
    std::atomic<int> requestedTriggerOutputIndex_{1};

    mutable uint64_t frameCount_{0};
    mutable std::chrono::steady_clock::time_point startTime_{};
    mutable WindowedRate frameRate_;
    mutable CameraStats lastStats_{};
};

} // namespace camera::common

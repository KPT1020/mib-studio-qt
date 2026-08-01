#pragma once

#include "backend/camera/common/ICamera.h"

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
    bool isRunning() const override { return running_; }

    bool grabFrame(Frame &out) override;
    bool pollStats(CameraStats &out) const override;
    bool checkDeviceHealth() const override;

    void configureTriggerOutput(const std::string &lineSelector) override;
    bool setTriggerOutput(bool high) override;

    // Software acquisition trigger (CameraSoftTrigger). Distinct from
    // setTriggerOutput, which drives the sort-output pulse.
    bool softTrigger() override;

private:
    bool applyJsonConfig(int hCamera);

    int cameraIndex_{-1};
    std::string configPath_;
    CameraConfig config_{};

    int hCamera_{-1};
    uint8_t *outBuffer_{nullptr};
    int bufferWidth_{0};
    int bufferHeight_{0};
    int triggerOutputIndex_{-1};
    // trigger_mode from the applied JSON config (0 continuous, 1 software,
    // 2 external) — checkDeviceHealth keys off it.
    int configuredTriggerMode_{0};

    bool running_{false};
    mutable std::mutex stateMutex_;

    mutable uint64_t frameCount_{0};
    mutable std::chrono::steady_clock::time_point startTime_{};
    mutable CameraStats lastStats_{};
};

} // namespace camera::common

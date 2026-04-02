#pragma once

#include "camera/common/ICamera.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

namespace camera::common {

/**
 * Concrete camera implementation backed by MindVision SDK.
 * Uses dynamic DLL loading (CameraApiLoad.h) - no .lib required at link time.
 *
 * Constructor takes the camera index from CameraEnumerateDevice and an optional
 * JSON config path.  When configPath is non-empty, start() applies the JSON
 * settings (resolution, exposure, trigger mode, gain) to the same camera
 * handle used for capture — guaranteeing the preview/capture session uses the
 * correct parameters regardless of whether settings persist across handles.
 */
class MindVisionCamera : public ICamera {
public:
    explicit MindVisionCamera(int cameraIndex, std::string configPath = {});
    ~MindVisionCamera() override;

    void applyConfig(const CameraConfig& config) override;
    bool start() override;
    void stop() override;
    bool isRunning() const override { return running_; }

    bool grabFrame(Frame& out) override;
    bool pollStats(CameraStats& out) const override;
    bool checkDeviceHealth() const override;

    void configureTriggerOutput(const std::string& lineSelector) override;
    bool setTriggerOutput(bool high) override;
    bool setExposureTime(double us) override;

private:
    int triggerOutputIndex_ = -1;  // MindVision output IO index (0-based), -1 = not configured
    // Apply JSON config (configPath_) to an already-open camera handle.
    // Returns true on success; logs warnings but does not abort on individual
    // SDK failures so that a partial apply is still better than none.
    bool applyJsonConfig(int hCamera);

    int cameraIndex_ = -1;
    std::string configPath_;    // Optional JSON config applied in start()
    CameraConfig config_{};

    int hCamera_ = -1;              // CameraHandle (int in MindVision SDK)
    uint8_t* outBuffer_ = nullptr;  // Aligned output buffer (MONO8)
    int bufferWidth_ = 0;
    int bufferHeight_ = 0;

    bool running_ = false;
    mutable std::mutex stateMutex_;

    // Rolling stats
    mutable uint64_t frameCount_ = 0;
    mutable std::chrono::steady_clock::time_point startTime_;
    mutable CameraStats lastStats_{};
};

} // namespace camera::common

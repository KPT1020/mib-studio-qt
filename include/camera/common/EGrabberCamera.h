#pragma once

#ifdef MIB_HAS_EGRABBER

#include "camera/common/ICamera.h"

#include <EGrabber.h>

#include <deque>
#include <optional>
#include <memory>
#include <mutex>

namespace camera::common {

/**
 * Concrete camera implementation backed by Euresys EGrabber.
 * The code path mirrors the official SDK sample
 * `egrabber-snippets/samples/310-high-frame-rate.cpp`.
 */
class EGrabberCamera : public ICamera {
public:
    EGrabberCamera();
    EGrabberCamera(int interfaceIndex, int deviceIndex);
    ~EGrabberCamera() override;

    void applyConfig(const CameraConfig& config) override;
    bool start() override;
    void stop() override;
    bool isRunning() const override { return running_; }

    bool grabFrame(Frame& out) override;
    bool pollStats(CameraStats& out) const override;
    bool checkDeviceHealth() const;

    void configureTriggerOutput(const std::string& lineSelector) override;
    bool setTriggerOutput(bool high) override;

private:
    void replenishPendingFrames();

    CameraConfig config_{};
    mutable std::unique_ptr<Euresys::EGrabber<Euresys::CallbackOnDemand>> grabber_;
    std::unique_ptr<Euresys::EGenTL> genTL_;

    uint64_t width_ = 0;
    uint64_t height_ = 0;

    // Optional target selection (interface/device indices)
    bool hasSelection_ = false;
    int selectedInterfaceIndex_ = -1;
    int selectedDeviceIndex_ = -1;

    mutable CameraStats lastStats_{};
    std::deque<Frame> pendingFrames_;
    bool running_ = false;
    mutable std::mutex stateMutex_;

    // Trigger output state
    std::string triggerLineSelector_;
    bool triggerConfigured_{false};
};

} // namespace camera::common

#endif // MIB_HAS_EGRABBER

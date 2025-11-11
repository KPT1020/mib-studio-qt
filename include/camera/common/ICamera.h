#pragma once

#include "camera/common/Frame.h"

#include <cstdint>

namespace camera::common {

struct CameraConfig {
    int bufferPartCount = 100;
    int numBuffers = 20;
};

struct CameraStats {
    uint64_t frameRate = 0;     // Frames per second as reported by the device.
    uint64_t dataRateMBps = 0;  // Throughput in MB/s.
};

class ICamera {
public:
    virtual ~ICamera() = default;

    virtual void applyConfig(const CameraConfig& config) = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;

    /**
     * Blocks until a frame is available or the camera is stopped.
     * Returns false when no further frames can be delivered (e.g. after stop()).
     */
    virtual bool grabFrame(Frame& out) = 0;

    /**
     * Poll device statistics. Returns false if stats are unavailable.
     */
    virtual bool pollStats(CameraStats& out) const = 0;
};

} // namespace camera::common



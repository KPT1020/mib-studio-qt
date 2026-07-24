#pragma once

#include "backend/camera/common/Frame.h"

#include <cstdint>
#include <string>

namespace camera::common
{

    struct CameraConfig
    {
        int bufferPartCount = 1;
        int numBuffers = 20;
    };

    struct CameraStats
    {
        uint64_t frameRate = 0;    // Frames per second as reported by the device.
        uint64_t dataRateMBps = 0; // Throughput in MB/s.
    };

    class ICamera
    {
    public:
        virtual ~ICamera() = default;

        virtual void applyConfig(const CameraConfig &config) = 0;
        virtual bool start() = 0;
        virtual void stop() = 0;
        virtual bool isRunning() const = 0;

        /**
         * Blocks until a frame is available or the camera is stopped.
         * Returns false when no further frames can be delivered (e.g. after stop()).
         */
        virtual bool grabFrame(Frame &out) = 0;

        /**
         * Poll device statistics. Returns false if stats are unavailable.
         */
        virtual bool pollStats(CameraStats &out) const = 0;

        /**
         * Check if device is healthy and responsive. Returns false if device is unresponsive.
         * Can be called periodically to detect device failures.
         */
        virtual bool checkDeviceHealth() const { return true; }

        /**
         * Configure a digital output line for trigger output.
         * No-op for cameras that don't support hardware trigger.
         * lineSelector is vendor-interpreted: EGrabber treats it as a GenICam
         * line name (e.g. "TTLIO12"); MindVision ignores it and uses the GPIO
         * index from its JSON config (trigger_output_index).
         */
        virtual void configureTriggerOutput(const std::string& lineSelector) { (void)lineSelector; }

        /**
         * Set trigger output line to High or Low. Returns false if not supported.
         */
        virtual bool setTriggerOutput(bool high) { (void)high; return false; }
    };

} // namespace camera::common

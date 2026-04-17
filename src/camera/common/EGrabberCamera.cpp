#ifdef MIB_HAS_EGRABBER
#include "camera/common/EGrabberCamera.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <stdexcept>
#include <vector>
#include <thread>
#include <chrono>
#include <mutex>

using namespace Euresys;

namespace camera::common {

EGrabberCamera::EGrabberCamera() = default;

EGrabberCamera::EGrabberCamera(int interfaceIndex, int deviceIndex)
    : hasSelection_(true),
      selectedInterfaceIndex_(interfaceIndex),
      selectedDeviceIndex_(deviceIndex) {}

EGrabberCamera::~EGrabberCamera() {
    stop();
}

void EGrabberCamera::applyConfig(const CameraConfig& config) {
    config_ = config;
}

bool EGrabberCamera::start() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    if (running_) {
        return true;
    }

    try {
        genTL_ = std::make_unique<EGenTL>();
        if (hasSelection_) {
            grabber_ = std::make_unique<EGrabber<CallbackOnDemand>>(
                *genTL_, selectedInterfaceIndex_, selectedDeviceIndex_);
        } else {
            grabber_ = std::make_unique<EGrabber<CallbackOnDemand>>(*genTL_);
        }

        // CRITICAL: Verify device is accessible before proceeding
        std::string deviceModel = "Unknown";
        std::string deviceVendor = "Unknown";
        try {
            deviceModel = grabber_->getString<DeviceModule>("DeviceModelName");
            try {
                deviceVendor = grabber_->getString<DeviceModule>("DeviceVendorName");
            } catch (const std::exception&) {
                // Vendor name may not be available, continue
            }
            SPDLOG_DEBUG("Camera device: {} {}", deviceVendor, deviceModel);
        } catch (const std::exception& ex) {
            SPDLOG_ERROR("Camera not accessible - device may be disconnected or in invalid state: {}", ex.what());
            grabber_.reset();
            genTL_.reset();
            return false;
        }

        // CRITICAL: Stop any existing acquisition first to ensure clean state
        try {
            grabber_->execute<RemoteModule>("AcquisitionStop");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            SPDLOG_DEBUG("EGrabberCamera: AcquisitionStop sent, waiting for camera to stabilize");
        } catch (const std::exception& ex) {
            // Graceful degradation: If AcquisitionStop fails, it may already be stopped
            // Log but continue - this is not a fatal error
            SPDLOG_DEBUG("EGrabberCamera AcquisitionStop before start (expected if not running): {}", ex.what());
        }

        // Follow SDK sample 310-high-frame-rate.cpp: probe resolution, then configure buffers.
        grabber_->setInteger<StreamModule>("BufferPartCount", 1);
        width_ = grabber_->getInteger<StreamModule>("Width");
        height_ = grabber_->getInteger<StreamModule>("Height");

        grabber_->setInteger<StreamModule>("BufferPartCount", config_.bufferPartCount);
        grabber_->reallocBuffers(config_.numBuffers);

        // CRITICAL: Start acquisition on remote device BEFORE starting grabber stream
        try {
            grabber_->execute<RemoteModule>("AcquisitionStart");
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            SPDLOG_DEBUG("EGrabberCamera: AcquisitionStart sent, camera ready");
        } catch (const std::exception& ex) {
            SPDLOG_ERROR("EGrabberCamera AcquisitionStart failed - camera may be in invalid state: {}", ex.what());
            // Clean up and return false - this is a fatal error
            grabber_.reset();
            genTL_.reset();
            return false;
        }

        grabber_->start();

        running_ = true;
        pendingFrames_.clear();

        SPDLOG_INFO("EGrabberCamera started successfully: {}x{}, parts={}, buffers={}, device={} {}",
                    width_, height_, config_.bufferPartCount, config_.numBuffers, deviceVendor, deviceModel);
        return true;
    } catch (const gentl_error& e) {
        SPDLOG_ERROR("EGrabberCamera start failed with GenTL error (code={}): {}", 
                    static_cast<int>(e.gc_err), e.what());
        stop();
        return false;
    } catch (const std::exception& ex) {
        SPDLOG_ERROR("EGrabberCamera start failed: {}", ex.what());
        stop();
        return false;
    }
}

void EGrabberCamera::stop() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    if (!running_) {
        return;
    }

    bool wasRunning = running_;
    running_ = false;
    
    // Wait a brief moment for any in-flight grabFrame() calls to finish
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    pendingFrames_.clear();

    if (grabber_) {
        try {
            // CRITICAL: Stop remote acquisition FIRST before stopping grabber stream
            try {
                grabber_->execute<RemoteModule>("AcquisitionStop");
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                SPDLOG_DEBUG("EGrabberCamera: AcquisitionStop sent, waiting for camera to stop");
            } catch (const std::exception& ex) {
                // Graceful degradation: If AcquisitionStop fails, it may already be stopped
                // Log but continue - this is not a fatal error
                SPDLOG_DEBUG("EGrabberCamera AcquisitionStop error (may already be stopped): {}", ex.what());
            }

            // Now stop the grabber stream
            grabber_->stop();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            SPDLOG_DEBUG("EGrabberCamera: grabber stream stopped");
            
        } catch (const gentl_error& e) {
            if (e.gc_err != gc::GC_ERR_ABORT) {
                SPDLOG_WARN("EGrabberCamera stop error: {}", e.what());
            } else {
                SPDLOG_DEBUG("EGrabberCamera stop aborted pending operations (expected): {}", e.what());
            }
        } catch (const std::exception& ex) {
            SPDLOG_WARN("EGrabberCamera stop error: {}", ex.what());
        }

        // Wake up any pending pop() to allow threads to exit cleanly
        try {
            grabber_->cancelPop();
        } catch (const std::exception& ex) {
            SPDLOG_DEBUG("EGrabberCamera cancelPop note: {}", ex.what());
        }

        // CRITICAL: Wait before releasing buffers to ensure they're all returned
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        try {
            grabber_->reallocBuffers(0);
            SPDLOG_DEBUG("EGrabberCamera: buffers released");
        } catch (const std::exception& ex) {
            SPDLOG_WARN("EGrabberCamera buffer release error: {}", ex.what());
        }

        // Final wait before destroying grabber
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    grabber_.reset();
    genTL_.reset();
    
    if (wasRunning) {
        SPDLOG_INFO("EGrabberCamera: fully stopped and cleaned up");
    }
}

bool EGrabberCamera::grabFrame(Frame& out) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    if (!running_) {
        return false;
    }

    try {
        if (pendingFrames_.empty()) {
            replenishPendingFrames();
        }

        if (pendingFrames_.empty()) {
            return false;
        }

        out = std::move(pendingFrames_.front());
        pendingFrames_.pop_front();
        return true;
    } catch (const gentl_error& e) {
        if (e.gc_err == gc::GC_ERR_ABORT) {
            // Normal during stop/shutdown
            SPDLOG_DEBUG("EGrabberCamera grab aborted (expected during stop): {}", e.what());
            return false;
        }
        SPDLOG_ERROR("EGrabberCamera grab failed with GenTL error (code={}): {}", 
                    static_cast<int>(e.gc_err), e.what());
        // Note: Don't call stop() here as it would try to acquire the same mutex
        running_ = false;
        return false;
    } catch (const std::exception& ex) {
        SPDLOG_ERROR("EGrabberCamera grab failed: {}", ex.what());
        // Note: Don't call stop() here as it would try to acquire the same mutex
        running_ = false;
        return false;
    }
}

bool EGrabberCamera::pollStats(CameraStats& out) const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    if (!running_ || !grabber_) {
        return false;
    }

    try {
        lastStats_.frameRate = grabber_->getInteger<StreamModule>("StatisticsFrameRate");
        lastStats_.dataRateMBps = grabber_->getInteger<StreamModule>("StatisticsDataRate");
        out = lastStats_;
        return true;
    } catch (const std::exception& ex) {
        SPDLOG_WARN("EGrabberCamera stats polling failed: {}", ex.what());
        return false;
    }
}

void EGrabberCamera::replenishPendingFrames() {
    // Note: This is called from grabFrame() which already holds the mutex
    // So we don't need to lock again here
    
    if (!grabber_) {
        return;
    }

    try {
        ScopedBuffer buffer(*grabber_);
        uint8_t* basePtr = buffer.getInfo<uint8_t*>(gc::BUFFER_INFO_BASE);
        const size_t imageSize = buffer.getInfo<size_t>(ge::BUFFER_INFO_CUSTOM_PART_SIZE);
        const size_t linePitch = buffer.getInfo<size_t>(ge::BUFFER_INFO_CUSTOM_LINE_PITCH);
        const uint64_t pixelFormat = buffer.getInfo<uint64_t>(gc::BUFFER_INFO_PIXELFORMAT);
        const uint64_t bufferTimestamp = buffer.getInfo<uint64_t>(gc::BUFFER_INFO_TIMESTAMP);
        const size_t delivered = buffer.getInfo<size_t>(ge::BUFFER_INFO_CUSTOM_NUM_DELIVERED_PARTS);

        std::vector<char> rawTimestamps = buffer.getInfo<std::vector<char>>(ge::BUFFER_INFO_CUSTOM_PART_TIMESTAMPS);
        const size_t tsCount = rawTimestamps.size() / sizeof(uint64_t);
        const uint64_t* timestamps = tsCount > 0 ? reinterpret_cast<const uint64_t*>(rawTimestamps.data()) : nullptr;

        for (size_t idx = 0; idx < delivered; ++idx) {
            const uint8_t* partPtr = basePtr + idx * imageSize;

            Frame frame;
            frame.width = width_;
            frame.height = height_;
            frame.pixelFormat = pixelFormat;
            frame.linePitch = linePitch == 0 ? static_cast<size_t>(width_) : linePitch;
            frame.timestamp = (timestamps && idx < tsCount) ? timestamps[idx] : bufferTimestamp;
            frame.data.resize(imageSize);
            std::copy_n(partPtr, imageSize, frame.data.begin());

            pendingFrames_.push_back(std::move(frame));
        }
    } catch (const gentl_error& e) {
        if (e.gc_err == gc::GC_ERR_ABORT) {
            // Expected during stop; do nothing
            SPDLOG_DEBUG("EGrabberCamera replenish aborted (expected during stop): {}", e.what());
            return;
        }
        throw;
    }
}

bool EGrabberCamera::checkDeviceHealth() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    if (!grabber_) {
        SPDLOG_DEBUG("Device health check: grabber not initialized");
        return false;
    }
    
    try {
        // Try to read a simple property to verify device is responsive
        std::string model = grabber_->getString<DeviceModule>("DeviceModelName");
        SPDLOG_TRACE("Device health check passed: {}", model);
        return true;
    } catch (const gentl_error& e) {
        SPDLOG_WARN("Device health check failed with GenTL error (code={}): {}", 
                   static_cast<int>(e.gc_err), e.what());
        return false;
    } catch (const std::exception& ex) {
        SPDLOG_WARN("Device health check failed: {}", ex.what());
        return false;
    }
}

void EGrabberCamera::configureTriggerOutput(const std::string& lineSelector) {
    triggerLineSelector_ = lineSelector;
    triggerConfigured_ = true;
    SPDLOG_INFO("EGrabberCamera: trigger output configured on {}", lineSelector);
}

bool EGrabberCamera::setTriggerOutput(bool high) {
    if (!triggerConfigured_ || !running_ || !grabber_) return false;
    try {
        // InterfaceModule operations are thread-safe vs StreamModule (frame grabbing),
        // so no mutex needed here. This runs on the trigger thread while
        // grabFrame() runs on the capture thread.
        grabber_->setString<Euresys::InterfaceModule>("LineSelector", triggerLineSelector_);
        grabber_->setString<Euresys::InterfaceModule>("LineSource", high ? "High" : "Low");
        return true;
    } catch (const std::exception& ex) {
        SPDLOG_WARN("EGrabberCamera trigger output failed: {}", ex.what());
        return false;
    }
}

} // namespace camera::common

#endif // MIB_HAS_EGRABBER

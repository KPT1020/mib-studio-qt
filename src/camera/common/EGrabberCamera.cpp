#include "camera/common/EGrabberCamera.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <stdexcept>
#include <vector>

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

        // Follow SDK sample 310-high-frame-rate.cpp: probe resolution, then configure buffers.
        grabber_->setInteger<StreamModule>("BufferPartCount", 1);
        width_ = grabber_->getInteger<StreamModule>("Width");
        height_ = grabber_->getInteger<StreamModule>("Height");

        grabber_->setInteger<StreamModule>("BufferPartCount", config_.bufferPartCount);
        grabber_->reallocBuffers(config_.numBuffers);
        grabber_->start();

        running_ = true;
        pendingFrames_.clear();

        SPDLOG_INFO("EGrabberCamera started: {}x{}, parts={}, buffers={}",
                    width_, height_, config_.bufferPartCount, config_.numBuffers);
        return true;
    } catch (const std::exception& ex) {
        SPDLOG_ERROR("EGrabberCamera start failed: {}", ex.what());
        stop();
        return false;
    }
}

void EGrabberCamera::stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    pendingFrames_.clear();

    if (grabber_) {
        try {
            grabber_->stop();
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

        try {
            grabber_->reallocBuffers(0);
        } catch (const std::exception& ex) {
            SPDLOG_WARN("EGrabberCamera buffer release error: {}", ex.what());
        }
    }

    grabber_.reset();
    genTL_.reset();
}

bool EGrabberCamera::grabFrame(Frame& out) {
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
        SPDLOG_ERROR("EGrabberCamera grab failed: {}", e.what());
        stop();
        return false;
    } catch (const std::exception& ex) {
        SPDLOG_ERROR("EGrabberCamera grab failed: {}", ex.what());
        stop();
        return false;
    }
}

bool EGrabberCamera::pollStats(CameraStats& out) const {
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

} // namespace camera::common



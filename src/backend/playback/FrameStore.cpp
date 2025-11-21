#include "backend/playback/FrameStore.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

namespace backend::playback {

FrameStore::FrameStore(size_t capacity)
    : capacity_(capacity), ring_(capacity) {}

void FrameStore::pushFrame(const uint8_t* src,
                           size_t size,
                           uint64_t width,
                           uint64_t height,
                           size_t linePitch,
                           uint64_t pixelFormat,
                           uint64_t timestamp) {
    if (capacity_ == 0 || src == nullptr || size == 0) return;
    const uint64_t w = totalWritten_.fetch_add(1) + 1; // next write count
    const size_t idx = static_cast<size_t>((w - 1) % capacity_);
    std::scoped_lock lk(mutex_);
    Frame& f = ring_[idx];
    f.width = width;
    f.height = height;
    f.pixelFormat = pixelFormat;
    f.linePitch = linePitch;
    f.timestamp = timestamp;
    f.data.resize(size);
    std::copy_n(src, size, f.data.begin());

    // Periodic stats
    if ((w % 1000ULL) == 0ULL) {
        const size_t avail = availableCount();
        SPDLOG_DEBUG("FrameStore: totalWritten={} available={} capacity={}",
                     static_cast<unsigned long long>(w), avail, capacity_);
    }
}

bool FrameStore::getLatest(Frame& out) const {
    const uint64_t w = totalWritten_.load();
    if (w == 0 || capacity_ == 0) return false;
    const size_t idx = static_cast<size_t>((w - 1) % capacity_);
    std::scoped_lock lk(mutex_);
    out = ring_[idx];
    return !out.data.empty();
}

bool FrameStore::getByWriteIndex(uint64_t writeIndex, Frame& out) const {
    const uint64_t w = totalWritten_.load();
    if (writeIndex >= w || capacity_ == 0) return false;
    const size_t idx = static_cast<size_t>(writeIndex % capacity_);
    std::scoped_lock lk(mutex_);
    out = ring_[idx];
    return !out.data.empty();
}

uint64_t FrameStore::earliestAvailableIndex() const {
    const uint64_t w = totalWritten_.load();
    if (capacity_ == 0 || w == 0) return 0;
    if (w <= capacity_) return 0;
    return w - static_cast<uint64_t>(capacity_);
}

uint64_t FrameStore::latestAvailableIndex() const {
    const uint64_t w = totalWritten_.load();
    if (w == 0) return 0;
    return w - 1;
}

size_t FrameStore::availableCount() const {
    const uint64_t w = totalWritten_.load();
    if (capacity_ == 0) return 0;
    return static_cast<size_t>(std::min<uint64_t>(w, static_cast<uint64_t>(capacity_)));
}

bool FrameStore::saveFramesToDisk(const std::string& outputDir, std::function<bool(const Frame&)> filterFn) const {
    const uint64_t earliest = earliestAvailableIndex();
    const uint64_t latest = latestAvailableIndex();
    if (latest < earliest) {
        SPDLOG_WARN("FrameStore: No frames available to save");
        return false;
    }
    return saveFramesToDisk(outputDir, earliest, latest, filterFn);
}

bool FrameStore::saveFramesToDisk(const std::string& outputDir, uint64_t startIndex, uint64_t endIndex,
                                  std::function<bool(const Frame&)> filterFn) const {
    std::unique_lock<std::mutex> lk(mutex_);

    const uint64_t earliest = earliestAvailableIndex();
    const uint64_t latest = latestAvailableIndex();
    const uint64_t w = totalWritten_.load();

    if (w == 0 || capacity_ == 0) {
        SPDLOG_WARN("FrameStore: No frames available to save");
        return false;
    }

    // Validate range
    if (startIndex > endIndex) {
        SPDLOG_ERROR("FrameStore: Invalid range: startIndex ({}) > endIndex ({})", startIndex, endIndex);
        return false;
    }

    if (endIndex < earliest || startIndex > latest) {
        SPDLOG_ERROR("FrameStore: Range [{}, {}] is outside available range [{}, {}]", 
                     startIndex, endIndex, earliest, latest);
        return false;
    }

    // Clamp range to available frames
    const uint64_t clampedStart = std::max(startIndex, earliest);
    const uint64_t clampedEnd = std::min(endIndex, latest);

    // Save frames - collect frames first, then save outside the lock
    std::vector<std::pair<uint64_t, Frame>> framesToSave;
    framesToSave.reserve(static_cast<size_t>(clampedEnd - clampedStart + 1));

    for (uint64_t idx = clampedStart; idx <= clampedEnd; ++idx) {
        Frame frame;
        // Access ring buffer directly since we hold the lock
        if (idx >= w) continue;
        const size_t ringIdx = static_cast<size_t>(idx % capacity_);
        if (ringIdx < ring_.size() && !ring_[ringIdx].data.empty()) {
            frame = ring_[ringIdx];  // Copy frame while holding lock
            framesToSave.emplace_back(idx, std::move(frame));
        } else {
            SPDLOG_WARN("FrameStore: Frame at index {} is not available", idx);
        }
    }

    // Release lock before saving (saving can take time and may involve I/O)
    lk.unlock();

    // Create output directory (outside lock)
    try {
        std::filesystem::create_directories(outputDir);
    } catch (const std::exception& ex) {
        SPDLOG_ERROR("FrameStore: Failed to create output directory {}: {}", outputDir, ex.what());
        return false;
    }

    // Now save frames
    size_t savedCount = 0;
    size_t failedCount = 0;

    size_t filteredCount = 0;
    for (const auto& pair : framesToSave) {
        const uint64_t idx = pair.first;
        const Frame& frame = pair.second;
        
        // Apply filter if provided
        if (filterFn && filterFn(frame)) {
            ++filteredCount;
            continue;
        }
        
        // Format filename with zero-padded 6-digit index: frame_000000.tiff
        char filenameBuf[64];
        std::snprintf(filenameBuf, sizeof(filenameBuf), "frame_%06llu.tiff", static_cast<unsigned long long>(idx));
        std::string filename = (std::filesystem::path(outputDir) / filenameBuf).string();
        
        SPDLOG_DEBUG("FrameStore: Saving frame {} to {}", idx, filename);
        if (saveFrameAsTiff(frame, filename)) {
            ++savedCount;
        } else {
            ++failedCount;
            SPDLOG_WARN("FrameStore: Failed to save frame at index {} to {}", idx, filename);
        }
    }
    
    if (filteredCount > 0) {
        SPDLOG_INFO("FrameStore: Filtered out {} empty frames", filteredCount);
    }

    SPDLOG_INFO("FrameStore: Saved {}/{} frames to {}", savedCount, clampedEnd - clampedStart + 1, outputDir);
    if (failedCount > 0) {
        SPDLOG_WARN("FrameStore: Failed to save {} frames", failedCount);
    }

    return failedCount == 0;
}

bool FrameStore::saveFramesToDisk(const std::string& outputDir, uint64_t startTimestamp, uint64_t endTimestamp, bool useTimestamps,
                                  std::function<bool(const Frame&)> filterFn) const {
    if (!useTimestamps) {
        // If useTimestamps is false, treat as indices
        return saveFramesToDisk(outputDir, startTimestamp, endTimestamp, filterFn);
    }

    uint64_t startIndex = 0;
    uint64_t endIndex = 0;
    if (!findIndicesByTimestampRange(startTimestamp, endTimestamp, startIndex, endIndex)) {
        SPDLOG_ERROR("FrameStore: No frames found in timestamp range [{}, {}]", startTimestamp, endTimestamp);
        return false;
    }

    return saveFramesToDisk(outputDir, startIndex, endIndex, filterFn);
}

bool FrameStore::resize(size_t newCapacity) {
    if (newCapacity == 0) {
        SPDLOG_ERROR("FrameStore: Cannot resize to zero capacity");
        return false;
    }

    std::scoped_lock lk(mutex_);

    const uint64_t w = totalWritten_.load();
    const size_t currentAvailable = availableCount();
    const uint64_t earliest = earliestAvailableIndex();

    // Create new ring buffer
    std::vector<Frame> newRing(newCapacity);

    if (newCapacity >= currentAvailable && w > 0) {
        // Preserve existing frames - access ring buffer directly since we hold the lock
        size_t preservedCount = 0;
        for (uint64_t idx = earliest; idx < earliest + currentAvailable && preservedCount < newCapacity; ++idx) {
            if (idx >= w) break;
            const size_t ringIdx = static_cast<size_t>(idx % capacity_);
            if (ringIdx < ring_.size() && !ring_[ringIdx].data.empty()) {
                newRing[preservedCount] = ring_[ringIdx];
                ++preservedCount;
            }
        }

        // Update totalWritten_ to reflect preserved frames
        // After resize, frames will be at indices 0 to preservedCount-1
        totalWritten_.store(static_cast<uint64_t>(preservedCount));

        SPDLOG_INFO("FrameStore: Resized from {} to {} capacity, preserved {}/{} frames", 
                    capacity_, newCapacity, preservedCount, currentAvailable);
    } else {
        // New capacity is smaller than available frames, clear buffer
        totalWritten_.store(0);
        SPDLOG_INFO("FrameStore: Resized from {} to {} capacity, cleared buffer (new size < available frames)", 
                    capacity_, newCapacity);
    }

    ring_ = std::move(newRing);
    capacity_ = newCapacity;

    return true;
}

IndexRange FrameStore::getAvailableRange() const {
    IndexRange range;
    range.start = earliestAvailableIndex();
    range.end = latestAvailableIndex();
    return range;
}

bool FrameStore::getAvailableTimestampRange(TimestampRange& out) const {
    const uint64_t earliest = earliestAvailableIndex();
    const uint64_t latest = latestAvailableIndex();
    const uint64_t w = totalWritten_.load();

    if (w == 0 || capacity_ == 0) {
        return false;
    }

    Frame firstFrame, lastFrame;
    if (!getByWriteIndex(earliest, firstFrame) || !getByWriteIndex(latest, lastFrame)) {
        return false;
    }

    out.start = firstFrame.timestamp;
    out.end = lastFrame.timestamp;
    return true;
}

bool FrameStore::saveFrameAsTiff(const Frame& frame, const std::string& filepath) const {
    SPDLOG_DEBUG("FrameStore: Attempting to save frame to {}", filepath);
    
    if (frame.data.empty() || frame.width == 0 || frame.height == 0) {
        SPDLOG_WARN("FrameStore: Invalid frame data for saving to {} (empty={}, width={}, height={})", 
                    filepath, frame.data.empty(), frame.width, frame.height);
        return false;
    }

    try {
        const int width = static_cast<int>(frame.width);
        const int height = static_cast<int>(frame.height);
        const size_t expectedSize = static_cast<size_t>(width * height);
        
        if (frame.data.size() < expectedSize) {
            SPDLOG_ERROR("FrameStore: Frame data size ({}) is less than expected ({})", 
                        frame.data.size(), expectedSize);
            return false;
        }

        // Calculate pitch - use linePitch if available, otherwise assume width
        const size_t pitch = frame.linePitch == 0 ? static_cast<size_t>(width) : frame.linePitch;
        
        // If pitch equals width, data is contiguous - we can use it directly
        // Otherwise, we need to copy to make it contiguous
        cv::Mat img;
        
        if (pitch == static_cast<size_t>(width)) {
            // Contiguous data - create Mat with reference to data
            SPDLOG_DEBUG("FrameStore: Creating Mat from contiguous data ({}x{})", width, height);
            img = cv::Mat(height, width, CV_8UC1, const_cast<uint8_t*>(frame.data.data()));
        } else {
            // Non-contiguous data - copy to make it contiguous
            SPDLOG_DEBUG("FrameStore: Copying non-contiguous data (pitch={}, width={})", pitch, width);
            img = cv::Mat(height, width, CV_8UC1);
            uint8_t* dst = img.data;
            const uint8_t* src = frame.data.data();
            for (int y = 0; y < height; ++y) {
                std::memcpy(dst + y * width, src + y * pitch, static_cast<size_t>(width));
            }
        }

        // Validate Mat before saving
        if (img.empty() || img.data == nullptr) {
            SPDLOG_ERROR("FrameStore: Failed to create valid OpenCV Mat");
            return false;
        }

        // Handle PFNC Mono8 format (0x01080001) or attempt anyway
        if (frame.pixelFormat != 0x01080001) {
            SPDLOG_WARN("FrameStore: Pixel format 0x{:016X} may not be Mono8, attempting save anyway", frame.pixelFormat);
        }

        // Save as TIFF
        std::vector<int> compression_params;
        compression_params.push_back(cv::IMWRITE_TIFF_COMPRESSION);
        compression_params.push_back(1); // LZW compression

        SPDLOG_DEBUG("FrameStore: Writing TIFF file {}", filepath);
        if (!cv::imwrite(filepath, img, compression_params)) {
            SPDLOG_ERROR("FrameStore: OpenCV cv::imwrite returned false for {}", filepath);
            return false;
        }

        SPDLOG_DEBUG("FrameStore: Successfully saved frame to {}", filepath);
        return true;
    } catch (const cv::Exception& ex) {
        SPDLOG_ERROR("FrameStore: OpenCV exception while saving frame to {}: {} (code={}, func={}, file={}, line={})", 
                    filepath, ex.what(), ex.code, ex.func, ex.file, ex.line);
        return false;
    } catch (const std::exception& ex) {
        SPDLOG_ERROR("FrameStore: Exception while saving frame to {}: {}", filepath, ex.what());
        return false;
    } catch (...) {
        SPDLOG_ERROR("FrameStore: Unknown exception while saving frame to {}", filepath);
        return false;
    }
}

bool FrameStore::findIndicesByTimestampRange(uint64_t startTimestamp, uint64_t endTimestamp, uint64_t& startIndex, uint64_t& endIndex) const {
    if (startTimestamp > endTimestamp) {
        return false;
    }

    const uint64_t earliest = earliestAvailableIndex();
    const uint64_t latest = latestAvailableIndex();
    const uint64_t w = totalWritten_.load();

    if (w == 0 || capacity_ == 0) {
        return false;
    }

    startIndex = UINT64_MAX;
    endIndex = 0;

    // Linear search through available frames
    for (uint64_t idx = earliest; idx <= latest; ++idx) {
        Frame frame;
        if (!getByWriteIndex(idx, frame)) {
            continue;
        }

        if (frame.timestamp >= startTimestamp && frame.timestamp <= endTimestamp) {
            if (startIndex == UINT64_MAX) {
                startIndex = idx;
            }
            endIndex = idx;
        }
    }

    return startIndex != UINT64_MAX;
}

} // namespace backend::playback



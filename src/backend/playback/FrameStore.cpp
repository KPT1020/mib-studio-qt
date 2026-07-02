#include "backend/playback/FrameStore.h"

#include "backend/diagnostics/CrashStateMirror.h"
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

namespace backend::playback {

FrameStore::FrameStore(size_t capacity)
    : capacity_(capacity), slotMutexes_(capacity), ring_(capacity),
      slotWriteIndices_(capacity, kSlotEmpty) {
    backend::diagnostics::CrashStateMirror::instance().frameStore.capacity.store(capacity_.load());
}

void FrameStore::reserveFrameBytes(size_t frameBytes) {
    if (frameBytes == 0) return;
    std::unique_lock structLk(structureMutex_);
    for (auto& f : ring_) {
        if (f.data.capacity() < frameBytes) f.data.reserve(frameBytes);
    }
    SPDLOG_INFO("FrameStore: reserved {} bytes per slot across {} slots", frameBytes, ring_.size());
}

void FrameStore::pushFrame(const uint8_t* src,
                           size_t size,
                           uint64_t width,
                           uint64_t height,
                           size_t linePitch,
                           uint64_t pixelFormat,
                           uint64_t timestamp) {
    if (capacity_.load(std::memory_order_acquire) == 0 || src == nullptr || size == 0) return;

    // Apply frame filter if set (check before acquiring write slot)
    {
        std::shared_lock structLk(structureMutex_);
        if (frameFilter_) {
            // Reuse a per-thread scratch Frame so filter inspection does not
            // allocate on every frame (assign reuses the existing capacity).
            thread_local Frame tmp;
            tmp.width = width;
            tmp.height = height;
            tmp.pixelFormat = pixelFormat;
            tmp.linePitch = linePitch;
            tmp.timestamp = timestamp;
            tmp.data.assign(src, src + size);
            if (frameFilter_(tmp)) {
                // Frame is empty / should be skipped
                totalFiltered_.fetch_add(1, std::memory_order_relaxed);
                backend::diagnostics::CrashStateMirror::instance().frameStore.totalFiltered
                    .fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
    }

    const uint64_t w = totalWritten_.fetch_add(1) + 1; // next write count
    {
        auto& fs = backend::diagnostics::CrashStateMirror::instance().frameStore;
        fs.totalWritten.store(w, std::memory_order_relaxed);
        fs.latestIndex.store(w - 1, std::memory_order_relaxed);
        fs.earliestIndex.store(w > capacity_.load(std::memory_order_acquire) ? w - capacity_.load(std::memory_order_acquire) : 0,
                               std::memory_order_relaxed);
    }
    // Copy into the ring under the structural SHARED lock (so a concurrent
    // resize cannot swap the buffers) plus this slot's own lock (so only a
    // reader of this exact slot can contend). The large memcpy below therefore
    // does not block consumers reading any other slot.
    {
        std::shared_lock structLk(structureMutex_);
        const size_t idx = static_cast<size_t>((w - 1) % capacity_.load(std::memory_order_acquire));
        std::scoped_lock slotLk(slotMutexes_[idx]);
        Frame& f = ring_[idx];
        f.width = width;
        f.height = height;
        f.pixelFormat = pixelFormat;
        f.linePitch = linePitch;
        f.timestamp = timestamp;
        f.data.resize(size);
        std::copy_n(src, size, f.data.begin());
        slotWriteIndices_[idx] = w - 1;
    }

    // Periodic stats
    if ((w % 5000ULL) == 0ULL) {
        const size_t avail = availableCount();
        const uint64_t filtered = totalFiltered_.load(std::memory_order_relaxed);
        SPDLOG_DEBUG("FrameStore: totalWritten={} available={} capacity={} filtered={}",
                     static_cast<unsigned long long>(w), avail, capacity_.load(std::memory_order_acquire), filtered);
    }
}

void FrameStore::setFrameFilter(FrameFilter filter) {
    std::unique_lock structLk(structureMutex_);
    frameFilter_ = std::move(filter);
    SPDLOG_INFO("FrameStore: Frame filter {}", frameFilter_ ? "enabled" : "cleared");
}

void FrameStore::clearFrameFilter() {
    std::unique_lock structLk(structureMutex_);
    frameFilter_ = nullptr;
    SPDLOG_INFO("FrameStore: Frame filter cleared");
}

bool FrameStore::hasFrameFilter() const {
    std::shared_lock structLk(structureMutex_);
    return frameFilter_ != nullptr;
}

bool FrameStore::getLatest(Frame& out) const {
    const uint64_t w = totalWritten_.load();
    if (w == 0 || capacity_.load(std::memory_order_acquire) == 0) return false;
    std::shared_lock structLk(structureMutex_);
    const size_t idx = static_cast<size_t>((w - 1) % capacity_.load(std::memory_order_acquire));
    std::scoped_lock slotLk(slotMutexes_[idx]);
    out = ring_[idx];
    return !out.data.empty();
}

bool FrameStore::getByWriteIndex(uint64_t writeIndex, Frame& out) const {
    const uint64_t w = totalWritten_.load();
    if (writeIndex >= w || capacity_.load(std::memory_order_acquire) == 0) return false;
    // Reject indices already evicted from the ring. Without this, the slot at
    // writeIndex % capacity holds a newer frame and we would silently return the
    // wrong frame instead of failing. Computed from the same `w` snapshot.
    if (w > static_cast<uint64_t>(capacity_.load(std::memory_order_acquire)) &&
        writeIndex < w - static_cast<uint64_t>(capacity_.load(std::memory_order_acquire))) {
        return false;
    }
    std::shared_lock structLk(structureMutex_);
    const size_t cap = capacity_.load(std::memory_order_acquire);
    const size_t idx = static_cast<size_t>(writeIndex % cap);
    std::scoped_lock slotLk(slotMutexes_[idx]);
    // Verify identity now that the slot is locked: totalWritten_ is
    // incremented before the slot data is copied, so the eviction check above
    // is a snapshot — a wrapping producer may have overwritten this slot with
    // a newer frame, or not yet written the frame this index refers to.
    if (slotWriteIndices_[idx] != writeIndex) {
        return false;
    }
    out = ring_[idx];
    return !out.data.empty();
}

bool FrameStore::getByWriteIndexROI(uint64_t writeIndex, int roiX, int roiY, int roiW, int roiH, Frame& out) const {
    const uint64_t w = totalWritten_.load();
    if (writeIndex >= w || capacity_.load(std::memory_order_acquire) == 0) return false;
    // Reject indices already evicted from the ring (see getByWriteIndex) so we
    // never extract an ROI from a newer frame aliased to the same slot.
    if (w > static_cast<uint64_t>(capacity_.load(std::memory_order_acquire)) &&
        writeIndex < w - static_cast<uint64_t>(capacity_.load(std::memory_order_acquire))) {
        return false;
    }
    std::shared_lock structLk(structureMutex_);
    const size_t cap = capacity_.load(std::memory_order_acquire);
    const size_t idx = static_cast<size_t>(writeIndex % cap);

    std::scoped_lock slotLk(slotMutexes_[idx]);
    // Verify identity under the slot lock (see getByWriteIndex): the slot may
    // hold a different frame — possibly with different geometry — than the
    // snapshot check above assumed.
    if (slotWriteIndices_[idx] != writeIndex) {
        return false;
    }
    const Frame& src = ring_[idx];
    if (src.data.empty() || src.width == 0 || src.height == 0) return false;
    
    // Clamp ROI to frame bounds
    const int frameW = static_cast<int>(src.width);
    const int frameH = static_cast<int>(src.height);
    const int clampedX = std::max(0, std::min(roiX, frameW - 1));
    const int clampedY = std::max(0, std::min(roiY, frameH - 1));
    const int clampedW = std::max(1, std::min(roiW, frameW - clampedX));
    const int clampedH = std::max(1, std::min(roiH, frameH - clampedY));
    
    // Copy frame metadata
    out.width = static_cast<uint64_t>(clampedW);
    out.height = static_cast<uint64_t>(clampedH);
    out.pixelFormat = src.pixelFormat;
    out.timestamp = src.timestamp;
    out.linePitch = 0; // ROI will be contiguous
    
    // Extract ROI region
    const size_t srcPitch = (src.linePitch == 0 ? static_cast<size_t>(src.width) : src.linePitch);
    // The ROI is clamped to width/height above, but the stride comes from the
    // producer: a frame whose data was sized to width*height while
    // linePitch > width would make the row walk below read past the buffer.
    const size_t requiredBytes = static_cast<size_t>(clampedY + clampedH - 1) * srcPitch +
                                 static_cast<size_t>(clampedX + clampedW);
    if (src.data.size() < requiredBytes) {
        SPDLOG_WARN("FrameStore: frame {} data ({} bytes) smaller than geometry requires "
                    "({}x{} pitch={} -> {} bytes); ROI read rejected",
                    writeIndex, src.data.size(), src.width, src.height, srcPitch, requiredBytes);
        return false;
    }
    const size_t roiSize = static_cast<size_t>(clampedW * clampedH);
    out.data.resize(roiSize);
    
    const uint8_t* srcPtr = src.data.data() + (clampedY * srcPitch) + clampedX;
    uint8_t* dstPtr = out.data.data();
    
    for (int y = 0; y < clampedH; ++y) {
        std::memcpy(dstPtr + y * clampedW, srcPtr + y * srcPitch, static_cast<size_t>(clampedW));
    }
    
    return true;
}

uint64_t FrameStore::earliestAvailableIndex() const {
    const uint64_t w = totalWritten_.load();
    if (capacity_.load(std::memory_order_acquire) == 0 || w == 0) return 0;
    if (w <= capacity_.load(std::memory_order_acquire)) return 0;
    return w - static_cast<uint64_t>(capacity_.load(std::memory_order_acquire));
}

uint64_t FrameStore::latestAvailableIndex() const {
    const uint64_t w = totalWritten_.load();
    if (w == 0) return 0;
    return w - 1;
}

size_t FrameStore::availableCount() const {
    const uint64_t w = totalWritten_.load();
    if (capacity_.load(std::memory_order_acquire) == 0) return 0;
    return static_cast<size_t>(std::min<uint64_t>(w, static_cast<uint64_t>(capacity_.load(std::memory_order_acquire))));
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
    std::unique_lock<std::shared_mutex> lk(structureMutex_);

    const uint64_t earliest = earliestAvailableIndex();
    const uint64_t latest = latestAvailableIndex();
    const uint64_t w = totalWritten_.load();

    if (w == 0 || capacity_.load(std::memory_order_acquire) == 0) {
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
        const size_t ringIdx = static_cast<size_t>(idx % capacity_.load(std::memory_order_acquire));
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

bool FrameStore::saveFramesToAvi(const std::string& outputPath,
                                 double fps,
                                 std::function<bool(const Frame&)> filterFn) const {
    const uint64_t earliest = earliestAvailableIndex();
    const uint64_t latest = latestAvailableIndex();
    if (latest < earliest) {
        SPDLOG_WARN("FrameStore: No frames available to save to AVI");
        return false;
    }
    return saveFramesToAvi(outputPath, earliest, latest, fps, filterFn);
}

bool FrameStore::saveFramesToAvi(const std::string& outputPath,
                                 uint64_t startIndex, uint64_t endIndex,
                                 double fps,
                                 std::function<bool(const Frame&)> filterFn) const {
    std::unique_lock<std::shared_mutex> lk(structureMutex_);

    const uint64_t earliest = earliestAvailableIndex();
    const uint64_t latest = latestAvailableIndex();
    const uint64_t w = totalWritten_.load();

    if (w == 0 || capacity_.load(std::memory_order_acquire) == 0) {
        SPDLOG_WARN("FrameStore: No frames available to save to AVI");
        return false;
    }

    if (startIndex > endIndex) {
        SPDLOG_ERROR("FrameStore: Invalid range: startIndex ({}) > endIndex ({})", startIndex, endIndex);
        return false;
    }

    if (endIndex < earliest || startIndex > latest) {
        SPDLOG_ERROR("FrameStore: Range [{}, {}] is outside available range [{}, {}]",
                     startIndex, endIndex, earliest, latest);
        return false;
    }

    const uint64_t clampedStart = std::max(startIndex, earliest);
    const uint64_t clampedEnd = std::min(endIndex, latest);

    // Snapshot frames under lock, then release for encoding
    std::vector<std::pair<uint64_t, Frame>> framesToSave;
    framesToSave.reserve(static_cast<size_t>(clampedEnd - clampedStart + 1));

    for (uint64_t idx = clampedStart; idx <= clampedEnd; ++idx) {
        if (idx >= w) continue;
        const size_t ringIdx = static_cast<size_t>(idx % capacity_.load(std::memory_order_acquire));
        if (ringIdx < ring_.size() && !ring_[ringIdx].data.empty()) {
            framesToSave.emplace_back(idx, ring_[ringIdx]);
        } else {
            SPDLOG_WARN("FrameStore: Frame at index {} is not available", idx);
        }
    }

    lk.unlock();

    // Ensure parent directory exists
    try {
        const std::filesystem::path p(outputPath);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path());
        }
    } catch (const std::exception& ex) {
        SPDLOG_ERROR("FrameStore: Failed to create parent directory for {}: {}", outputPath, ex.what());
        return false;
    }

    return writeFramesAsAvi(framesToSave, outputPath, fps, filterFn);
}

bool FrameStore::saveFramesToAvi(const std::string& outputPath,
                                 uint64_t startTimestamp, uint64_t endTimestamp, bool useTimestamps,
                                 double fps,
                                 std::function<bool(const Frame&)> filterFn) const {
    if (!useTimestamps) {
        return saveFramesToAvi(outputPath, startTimestamp, endTimestamp, fps, filterFn);
    }

    uint64_t startIndex = 0;
    uint64_t endIndex = 0;
    if (!findIndicesByTimestampRange(startTimestamp, endTimestamp, startIndex, endIndex)) {
        SPDLOG_ERROR("FrameStore: No frames found in timestamp range [{}, {}]", startTimestamp, endTimestamp);
        return false;
    }

    return saveFramesToAvi(outputPath, startIndex, endIndex, fps, filterFn);
}

bool FrameStore::writeFramesAsAvi(const std::vector<std::pair<uint64_t, Frame>>& frames,
                                  const std::string& outputPath,
                                  double fps,
                                  std::function<bool(const Frame&)> filterFn) const {
    if (frames.empty()) {
        SPDLOG_WARN("FrameStore: No frames to write to AVI");
        return false;
    }

    if (fps <= 0.0) fps = 30.0;

    // Determine frame size from the first non-empty frame
    int frameWidth = 0;
    int frameHeight = 0;
    for (const auto& pair : frames) {
        if (pair.second.width > 0 && pair.second.height > 0) {
            frameWidth = static_cast<int>(pair.second.width);
            frameHeight = static_cast<int>(pair.second.height);
            break;
        }
    }
    if (frameWidth <= 0 || frameHeight <= 0) {
        SPDLOG_ERROR("FrameStore: Cannot determine frame dimensions for AVI");
        return false;
    }

    // Try Y800 (grayscale) first, fall back to uncompressed BGR DIB.
    cv::VideoWriter writer;
    bool useColorFallback = false;
    const cv::Size frameSize(frameWidth, frameHeight);

    const int fourccY800 = cv::VideoWriter::fourcc('Y', '8', '0', '0');
    try {
        writer.open(outputPath, fourccY800, fps, frameSize, /*isColor=*/false);
    } catch (const cv::Exception& ex) {
        SPDLOG_DEBUG("FrameStore: Y800 VideoWriter open threw: {}", ex.what());
    }

    if (!writer.isOpened()) {
        SPDLOG_INFO("FrameStore: Y800 AVI writer unavailable; falling back to uncompressed BGR (DIB )");
        const int fourccDib = cv::VideoWriter::fourcc('D', 'I', 'B', ' ');
        try {
            writer.open(outputPath, fourccDib, fps, frameSize, /*isColor=*/true);
        } catch (const cv::Exception& ex) {
            SPDLOG_ERROR("FrameStore: DIB VideoWriter open threw: {}", ex.what());
        }
        useColorFallback = true;
    }

    if (!writer.isOpened()) {
        SPDLOG_ERROR("FrameStore: Failed to open AVI writer for {}", outputPath);
        return false;
    }

    SPDLOG_INFO("FrameStore: Writing AVI {} ({}x{} @ {:.2f} fps, {})",
                outputPath, frameWidth, frameHeight, fps,
                useColorFallback ? "DIB/BGR" : "Y800/GRAY");

    size_t writtenCount = 0;
    size_t filteredCount = 0;
    size_t skippedCount = 0;

    try {
        for (const auto& pair : frames) {
            const Frame& frame = pair.second;

            if (filterFn && filterFn(frame)) {
                ++filteredCount;
                continue;
            }

            if (frame.data.empty() || frame.width == 0 || frame.height == 0) {
                ++skippedCount;
                continue;
            }

            if (static_cast<int>(frame.width) != frameWidth ||
                static_cast<int>(frame.height) != frameHeight) {
                SPDLOG_WARN("FrameStore: Frame at index {} has mismatched dimensions ({}x{} vs {}x{}); skipping",
                            pair.first, frame.width, frame.height, frameWidth, frameHeight);
                ++skippedCount;
                continue;
            }

            const int w = static_cast<int>(frame.width);
            const int h = static_cast<int>(frame.height);
            const size_t pitch = frame.linePitch == 0 ? static_cast<size_t>(w) : frame.linePitch;
            // Pitch-aware: the strided row walk below reads up to
            // (h-1)*pitch + w bytes, more than w*h when pitch > w.
            const size_t expectedSize = static_cast<size_t>(h - 1) * pitch + static_cast<size_t>(w);
            if (frame.data.size() < expectedSize) {
                SPDLOG_WARN("FrameStore: Frame {} data too small ({} < {}); skipping",
                            pair.first, frame.data.size(), expectedSize);
                ++skippedCount;
                continue;
            }
            cv::Mat gray;
            if (pitch == static_cast<size_t>(w)) {
                gray = cv::Mat(h, w, CV_8UC1, const_cast<uint8_t*>(frame.data.data())).clone();
            } else {
                gray = cv::Mat(h, w, CV_8UC1);
                uint8_t* dst = gray.data;
                const uint8_t* src = frame.data.data();
                for (int y = 0; y < h; ++y) {
                    std::memcpy(dst + y * w, src + y * pitch, static_cast<size_t>(w));
                }
            }

            if (useColorFallback) {
                cv::Mat bgr;
                cv::cvtColor(gray, bgr, cv::COLOR_GRAY2BGR);
                writer.write(bgr);
            } else {
                writer.write(gray);
            }
            ++writtenCount;
        }
    } catch (const cv::Exception& ex) {
        SPDLOG_ERROR("FrameStore: OpenCV exception during AVI write: {}", ex.what());
        writer.release();
        return false;
    } catch (const std::exception& ex) {
        SPDLOG_ERROR("FrameStore: Exception during AVI write: {}", ex.what());
        writer.release();
        return false;
    }

    writer.release();

    SPDLOG_INFO("FrameStore: Wrote {} frames to AVI {} (filtered {}, skipped {})",
                writtenCount, outputPath, filteredCount, skippedCount);

    return writtenCount > 0;
}

bool FrameStore::resize(size_t newCapacity) {
    if (newCapacity == 0) {
        SPDLOG_ERROR("FrameStore: Cannot resize to zero capacity");
        return false;
    }

    std::unique_lock<std::shared_mutex> lk(structureMutex_);

    const uint64_t w = totalWritten_.load();
    const size_t oldCapacity = capacity_.load(std::memory_order_acquire);

    // Create new ring buffer
    std::vector<Frame> newRing(newCapacity);
    std::vector<uint64_t> newSlotIndices(newCapacity, kSlotEmpty);

    // Preserve up to newCapacity of the most-recent frames, keyed by their
    // ORIGINAL absolute write index (never renumbered) — this is exactly the
    // "earliest available" computation, evaluated as if newCapacity had been
    // in effect all along. totalWritten_ is deliberately NEVER reset or
    // rewound by resize(): absolute indices stay globally unique for the
    // life of the FrameStore, so a writeIndex a caller obtained before this
    // resize can never spuriously collide with a *different* frame that
    // happens to reuse the same small number after the resize (the bug a
    // renumbering scheme reintroduces) — it either still names the same
    // frame (if retained) or fails the ordinary w/earliest bounds check
    // exactly as it would without any resize at all.
    const uint64_t newEarliest = (w > static_cast<uint64_t>(newCapacity)) ? (w - newCapacity) : 0;
    size_t preservedCount = 0;
    for (uint64_t idx = newEarliest; idx < w; ++idx) {
        const size_t oldRingIdx = static_cast<size_t>(idx % oldCapacity);
        // On a GROWING resize, newEarliest can reach further back than the old
        // ring could ever have retained (oldCapacity < newCapacity): once the
        // ring has wrapped, ring_[oldRingIdx] is physically shared by idx,
        // idx+oldCapacity, idx+2*oldCapacity, ... To copy it into the new ring
        // under the label `idx` we must first confirm the old ring's own
        // identity array agrees this slot currently holds frame `idx` — the
        // same identity check getByWriteIndex uses. Without it, a slot holding
        // a NEWER frame would be silently relabeled with the OLDER idx.
        if (oldRingIdx < ring_.size() && !ring_[oldRingIdx].data.empty() &&
            slotWriteIndices_[oldRingIdx] == idx) {
            const size_t newRingIdx = static_cast<size_t>(idx % newCapacity);
            newRing[newRingIdx] = ring_[oldRingIdx];
            newSlotIndices[newRingIdx] = idx;
            ++preservedCount;
        }
    }

    SPDLOG_INFO("FrameStore: Resized from {} to {} capacity, preserved {} frame(s) "
                "(absolute indices [{}, {}))",
                oldCapacity, newCapacity, preservedCount, newEarliest, w);

    // Rebuild the per-slot lock array to match the new ring. Safe under the
    // exclusive structural lock: no hot-path op can hold a slot lock here
    // because each acquires the structural lock in shared mode first.
    std::vector<std::mutex> newMutexes(newCapacity);
    slotMutexes_.swap(newMutexes);

    ring_ = std::move(newRing);
    slotWriteIndices_ = std::move(newSlotIndices);
    capacity_.store(newCapacity, std::memory_order_release);
    backend::diagnostics::CrashStateMirror::instance().frameStore.capacity.store(capacity_.load());

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

    if (w == 0 || capacity_.load(std::memory_order_acquire) == 0) {
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

size_t FrameStore::estimateMemoryBytesForCapacity(size_t capacity) const {
    if (capacity == 0) {
        return 0;
    }

    // Try to calculate average frame size from existing frames
    size_t avgFrameSize = 0;
    size_t sampleCount = 0;
    const size_t maxSamples = 10; // Sample up to 10 frames for average

    // Exclusive: samples slot data directly, so it must exclude a concurrent
    // producer overwriting those slots (same guarantee the old global mutex gave).
    std::unique_lock<std::shared_mutex> structLk(structureMutex_);
    const uint64_t w = totalWritten_.load();
    const size_t available = availableCount();

    if (available > 0 && w > 0) {
        const uint64_t earliest = earliestAvailableIndex();
        const uint64_t latest = latestAvailableIndex();
        const size_t sampleStep = std::max<size_t>(1, available / maxSamples);

        size_t totalSize = 0;
        for (uint64_t idx = earliest; idx <= latest && sampleCount < maxSamples; idx += sampleStep) {
            if (idx >= w) break;
            const size_t ringIdx = static_cast<size_t>(idx % capacity_.load(std::memory_order_acquire));
            if (ringIdx < ring_.size() && !ring_[ringIdx].data.empty()) {
                // Frame size = data size + overhead (Frame struct + vector overhead)
                // Frame struct: ~48 bytes (width, height, pixelFormat, linePitch, timestamp)
                // Vector overhead: typically 24 bytes on 64-bit systems
                const size_t frameOverhead = 48 + 24;
                totalSize += ring_[ringIdx].data.size() + frameOverhead;
                ++sampleCount;
            }
        }

        if (sampleCount > 0) {
            avgFrameSize = totalSize / sampleCount;
        }
    }

    // If no frames available, use conservative default: 1920x1080 Mono8 = ~2MB per frame
    if (avgFrameSize == 0) {
        // 1920 * 1080 * 1 byte per pixel = 2,073,600 bytes
        // Add overhead for Frame struct and vector
        const size_t defaultFrameData = 1920ULL * 1080ULL;
        const size_t frameOverhead = 48 + 24;
        avgFrameSize = defaultFrameData + frameOverhead;
    }

    // Total memory = capacity * average frame size
    // Add some overhead for the vector container itself
    const size_t containerOverhead = sizeof(std::vector<Frame>) + (capacity * sizeof(Frame));
    return (capacity * avgFrameSize) + containerOverhead;
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
        // Calculate pitch - use linePitch if available, otherwise assume width
        const size_t pitch = frame.linePitch == 0 ? static_cast<size_t>(width) : frame.linePitch;
        // Pitch-aware: the strided access below reads up to
        // (height-1)*pitch + width bytes, more than width*height when pitch > width.
        const size_t expectedSize = static_cast<size_t>(height - 1) * pitch + static_cast<size_t>(width);

        if (frame.data.size() < expectedSize) {
            SPDLOG_ERROR("FrameStore: Frame data size ({}) is less than expected ({})",
                        frame.data.size(), expectedSize);
            return false;
        }
        
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

    if (w == 0 || capacity_.load(std::memory_order_acquire) == 0) {
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



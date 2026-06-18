#pragma once

#include <cstdint>
#include <vector>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <string>
#include <functional>

namespace backend::playback
{

    struct Frame
    {
        uint64_t width = 0;
        uint64_t height = 0;
        uint64_t pixelFormat = 0; // PFNC code from Euresys
        size_t linePitch = 0;     // bytes per line
        uint64_t timestamp = 0;   // raw unit from source
        std::vector<uint8_t> data;
    };

    struct IndexRange {
        uint64_t start = 0;
        uint64_t end = 0;
    };

    struct TimestampRange {
        uint64_t start = 0;
        uint64_t end = 0;
    };

    class FrameStore
    {
    public:
        explicit FrameStore(size_t capacity = 512);

        // Copy frame data into the ring buffer
        void pushFrame(const uint8_t *src,
                       size_t size,
                       uint64_t width,
                       uint64_t height,
                       size_t linePitch,
                       uint64_t pixelFormat,
                       uint64_t timestamp);

        // Retrieve a copy of the latest frame; returns false if empty
        bool getLatest(Frame &out) const;

        // Retrieve a copy by absolute write index; returns false if out-of-range
        bool getByWriteIndex(uint64_t writeIndex, Frame &out) const;

        // Retrieve ROI region from frame by absolute write index without full frame copy
        // Returns false if out-of-range or invalid ROI
        // The ROI is extracted directly from the frame data, avoiding full frame copy
        bool getByWriteIndexROI(uint64_t writeIndex, int roiX, int roiY, int roiW, int roiH, Frame &out) const;

        // Absolute index helpers (monotonic sequence since start)
        // Earliest absolute index currently retained in the ring
        uint64_t earliestAvailableIndex() const;

        // Latest absolute index written (w-1). Only valid when totalWritten() > 0
        uint64_t latestAvailableIndex() const;

        // Number of frames currently retained in the ring (<= capacity)
        size_t availableCount() const;

        // Current number of frames written since start (monotonic)
        uint64_t totalWritten() const { return totalWritten_.load(); }

        // Ring capacity
        size_t capacity() const { return capacity_; }

        // Save frames to disk as TIFF images
        // Saves all available frames if range not specified
        // filterFn: optional function that returns true if frame should be skipped (empty frames)
        bool saveFramesToDisk(const std::string& outputDir, 
                              std::function<bool(const Frame&)> filterFn = nullptr) const;

        // Save frames by index range (startIndex inclusive, endIndex inclusive)
        // filterFn: optional function that returns true if frame should be skipped (empty frames)
        bool saveFramesToDisk(const std::string& outputDir, uint64_t startIndex, uint64_t endIndex,
                              std::function<bool(const Frame&)> filterFn = nullptr) const;

        // Save frames by timestamp range (startTimestamp inclusive, endTimestamp inclusive)
        // filterFn: optional function that returns true if frame should be skipped (empty frames)
        bool saveFramesToDisk(const std::string& outputDir, uint64_t startTimestamp, uint64_t endTimestamp, bool useTimestamps,
                              std::function<bool(const Frame&)> filterFn = nullptr) const;

        // Save frames to a single uncompressed AVI file.
        // Tries Y800 (grayscale) FourCC first, falls back to uncompressed BGR ("DIB ")
        // if the Y800 writer fails to open. Per-frame timestamps are NOT preserved.
        // filterFn: optional function that returns true if frame should be skipped.
        // fps: playback frame rate embedded in the AVI header (does not affect content).
        bool saveFramesToAvi(const std::string& outputPath, double fps = 30.0,
                             std::function<bool(const Frame&)> filterFn = nullptr) const;

        // Save frames to AVI by index range (inclusive).
        bool saveFramesToAvi(const std::string& outputPath, uint64_t startIndex, uint64_t endIndex,
                             double fps = 30.0,
                             std::function<bool(const Frame&)> filterFn = nullptr) const;

        // Save frames to AVI by timestamp range (inclusive).
        bool saveFramesToAvi(const std::string& outputPath, uint64_t startTimestamp, uint64_t endTimestamp,
                             bool useTimestamps, double fps = 30.0,
                             std::function<bool(const Frame&)> filterFn = nullptr) const;

        // Resize buffer capacity safely
        // Preserves existing frames when possible (if new size >= current available frames)
        // Clears buffer if new size < current available frames
        // Returns true on success, false on failure
        bool resize(size_t newCapacity);

        // Get available index range
        IndexRange getAvailableRange() const;

        // Get available timestamp range
        // Returns false if no frames available
        bool getAvailableTimestampRange(TimestampRange& out) const;

        // Estimate memory in bytes needed for a given capacity
        // Uses average frame size from existing frames if available, otherwise uses conservative default
        size_t estimateMemoryBytesForCapacity(size_t capacity) const;

        // Frame filter for recording mode
        // When set, pushFrame() will call this filter before storing.
        // The filter receives a temporary Frame reference and returns true if the frame should be SKIPPED (filtered out).
        using FrameFilter = std::function<bool(const Frame&)>;
        void setFrameFilter(FrameFilter filter);
        void clearFrameFilter();
        bool hasFrameFilter() const;

        // Number of frames filtered (skipped) since start or last reset
        uint64_t totalFiltered() const { return totalFiltered_.load(); }
        void resetFilteredCount() { totalFiltered_.store(0); }

    private:
        size_t capacity_;
        // Structural lock: guards the identity of ring_ / slotMutexes_, capacity_
        // and frameFilter_. Held in SHARED mode by the per-frame hot path
        // (pushFrame / getByWriteIndex / getLatest) so producer and consumers do
        // not serialize against each other, and in EXCLUSIVE mode only by rare
        // whole-ring operations (resize / save / estimate) that replace the ring
        // or touch many slots at once.
        mutable std::shared_mutex structureMutex_;
        // Per-slot locks (parallel to ring_): a single slot lock is held only
        // while copying one frame in/out, so the large memcpy no longer happens
        // under a global mutex. The producer writing slot A never blocks a
        // consumer reading slot B.
        mutable std::vector<std::mutex> slotMutexes_;
        std::vector<Frame> ring_;
        std::atomic<uint64_t> totalWritten_{0};
        std::atomic<uint64_t> totalFiltered_{0};

        // Frame filter (protected by structureMutex_)
        FrameFilter frameFilter_;

        // Internal helper to save a single frame as TIFF
        bool saveFrameAsTiff(const Frame& frame, const std::string& filepath) const;

        // Internal helper: write already-collected frames to an AVI file.
        // Opens cv::VideoWriter once, writes sequentially, closes on return.
        // Must be called WITHOUT holding mutex_.
        bool writeFramesAsAvi(const std::vector<std::pair<uint64_t, Frame>>& frames,
                              const std::string& outputPath,
                              double fps,
                              std::function<bool(const Frame&)> filterFn) const;

        // Internal helper to find frame indices by timestamp range
        bool findIndicesByTimestampRange(uint64_t startTimestamp, uint64_t endTimestamp, uint64_t& startIndex, uint64_t& endIndex) const;
    };

} // namespace backend::playback

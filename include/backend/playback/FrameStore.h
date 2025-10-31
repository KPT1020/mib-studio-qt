#pragma once

#include <cstdint>
#include <vector>
#include <mutex>
#include <atomic>

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

        // Current number of frames written since start (monotonic)
        uint64_t totalWritten() const { return totalWritten_.load(); }

        // Ring capacity
        size_t capacity() const { return capacity_; }

    private:
        const size_t capacity_;
        mutable std::mutex mutex_;
        std::vector<Frame> ring_;
        std::atomic<uint64_t> totalWritten_{0};
    };

} // namespace backend::playback

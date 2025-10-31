#include "backend/playback/FrameStore.h"

#include <algorithm>

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

} // namespace backend::playback



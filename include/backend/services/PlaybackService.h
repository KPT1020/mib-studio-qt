#pragma once

#include <memory>
#include <cstdint>

namespace backend::playback { struct Frame; class FrameStore; }

namespace backend::services {

class PlaybackService {
public:
    PlaybackService();
    ~PlaybackService();

    void setFrameStore(std::shared_ptr<backend::playback::FrameStore> store);

    void play();
    void pause();
    bool isPlaying() const { return playing_; }

    // Fetch latest frame copy from store; returns false if none
    bool fetchLatest(backend::playback::Frame& out) const;

    // Fetch a frame by absolute index; returns false if out of range
    bool fetchByIndex(uint64_t absoluteIndex, backend::playback::Frame& out) const;

    // Query current available absolute index window
    // Returns false if store is empty
    bool queryRange(uint64_t& outEarliest, uint64_t& outLatest, size_t& outCount) const;

    // Convenience accessors
    uint64_t totalWritten() const;
    size_t capacity() const;

private:
    std::shared_ptr<backend::playback::FrameStore> store_{};
    bool playing_{true};
};

} // namespace backend::services



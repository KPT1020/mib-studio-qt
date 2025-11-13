#pragma once

#include <memory>
#include <cstdint>
#include <string>

namespace backend::playback { struct Frame; class FrameStore; struct IndexRange; struct TimestampRange; }

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

    // Save frames to disk as TIFF images
    // Saves all available frames if range not specified
    bool saveFramesToDisk(const std::string& outputDir) const;

    // Save frames by index range (startIndex inclusive, endIndex inclusive)
    bool saveFramesToDisk(const std::string& outputDir, uint64_t startIndex, uint64_t endIndex) const;

    // Save frames by timestamp range (startTimestamp inclusive, endTimestamp inclusive)
    bool saveFramesToDisk(const std::string& outputDir, uint64_t startTimestamp, uint64_t endTimestamp, bool useTimestamps) const;

    // Resize buffer capacity safely
    // Preserves existing frames when possible (if new size >= current available frames)
    // Clears buffer if new size < current available frames
    // Returns true on success, false on failure
    bool resize(size_t newCapacity);

    // Get available index range
    backend::playback::IndexRange getAvailableRange() const;

    // Get available timestamp range
    // Returns false if no frames available
    bool getAvailableTimestampRange(backend::playback::TimestampRange& out) const;

private:
    std::shared_ptr<backend::playback::FrameStore> store_{};
    bool playing_{true};
};

} // namespace backend::services



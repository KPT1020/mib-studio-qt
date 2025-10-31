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

private:
    std::shared_ptr<backend::playback::FrameStore> store_{};
    bool playing_{true};
};

} // namespace backend::services



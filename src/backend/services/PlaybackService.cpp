#include "backend/services/PlaybackService.h"
#include "backend/playback/FrameStore.h"

#include <spdlog/spdlog.h>

namespace backend::services {

PlaybackService::PlaybackService() = default;
PlaybackService::~PlaybackService() = default;

void PlaybackService::setFrameStore(std::shared_ptr<backend::playback::FrameStore> store) {
    store_ = std::move(store);
}

void PlaybackService::play() {
    playing_ = true;
    SPDLOG_INFO("PlaybackService: play");
}

void PlaybackService::pause() {
    playing_ = false;
    SPDLOG_INFO("PlaybackService: pause");
}

bool PlaybackService::fetchLatest(backend::playback::Frame& out) const {
    if (!store_) return false;
    return store_->getLatest(out);
}

} // namespace backend::services



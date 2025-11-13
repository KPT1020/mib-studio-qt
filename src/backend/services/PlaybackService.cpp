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

bool PlaybackService::fetchByIndex(uint64_t absoluteIndex, backend::playback::Frame& out) const {
    if (!store_) return false;
    return store_->getByWriteIndex(absoluteIndex, out);
}

bool PlaybackService::queryRange(uint64_t& outEarliest, uint64_t& outLatest, size_t& outCount) const {
    if (!store_) return false;
    const uint64_t latest = store_->latestAvailableIndex();
    const size_t count = store_->availableCount();
    if (count == 0) return false;
    const uint64_t earliest = store_->earliestAvailableIndex();
    outEarliest = earliest;
    outLatest = latest;
    outCount = count;
    return true;
}

uint64_t PlaybackService::totalWritten() const {
    if (!store_) return 0;
    return store_->totalWritten();
}

size_t PlaybackService::capacity() const {
    if (!store_) return 0;
    return store_->capacity();
}

bool PlaybackService::saveFramesToDisk(const std::string& outputDir) const {
    if (!store_) return false;
    return store_->saveFramesToDisk(outputDir);
}

bool PlaybackService::saveFramesToDisk(const std::string& outputDir, uint64_t startIndex, uint64_t endIndex) const {
    if (!store_) return false;
    return store_->saveFramesToDisk(outputDir, startIndex, endIndex);
}

bool PlaybackService::saveFramesToDisk(const std::string& outputDir, uint64_t startTimestamp, uint64_t endTimestamp, bool useTimestamps) const {
    if (!store_) return false;
    return store_->saveFramesToDisk(outputDir, startTimestamp, endTimestamp, useTimestamps);
}

bool PlaybackService::resize(size_t newCapacity) {
    if (!store_) return false;
    return store_->resize(newCapacity);
}

backend::playback::IndexRange PlaybackService::getAvailableRange() const {
    if (!store_) {
        backend::playback::IndexRange empty;
        empty.start = 0;
        empty.end = 0;
        return empty;
    }
    return store_->getAvailableRange();
}

bool PlaybackService::getAvailableTimestampRange(backend::playback::TimestampRange& out) const {
    if (!store_) return false;
    return store_->getAvailableTimestampRange(out);
}

} // namespace backend::services



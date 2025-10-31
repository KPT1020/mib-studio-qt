#include "backend/AppBackend.h"

#include "backend/services/Logger.h"
#include "backend/services/SqliteService.h"
#include "backend/services/Hdf5Service.h"
#include "backend/services/CaptureService.h"
#include "backend/services/ProcessingService.h"
#include "backend/services/PlaybackService.h"
#include "backend/playback/FrameStore.h"

#include <algorithm>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace backend {

AppBackend::AppBackend() = default;
AppBackend::~AppBackend() = default;

bool AppBackend::initialize(const std::string& dataDir) {
    std::filesystem::create_directories(dataDir);

    backend::services::Logger::init((std::filesystem::path(dataDir) / "logs" / "app.log").string());

    sqliteService_ = std::make_unique<services::SqliteService>();
    hdf5Service_ = std::make_unique<services::Hdf5Service>();
    captureService_ = std::make_unique<services::CaptureService>();
    processingService_ = std::make_unique<services::ProcessingService>();
    playbackService_ = std::make_unique<services::PlaybackService>();
    frameStore_ = std::make_shared<playback::FrameStore>(512);

    sqliteService_->initialize((std::filesystem::path(dataDir) / "app.sqlite3").string());
    hdf5Service_->initialize(dataDir);

    processingService_->start();

    // Wire capture -> frame store for playback/display
    captureService_->setFrameStore(frameStore_);
    playbackService_->setFrameStore(frameStore_);

    // Wire capture -> processing (CPU-only): compute a tiny checksum snapshot and enqueue a lightweight job
    captureService_->setFrameCallback([this](const uint8_t* data,
                                             size_t size,
                                             uint64_t width,
                                             uint64_t height,
                                             uint64_t timestampNs) {
        const size_t sampleSize = std::min<size_t>(size, 64);
        uint32_t checksum = 0;
        for (size_t i = 0; i < sampleSize; ++i) checksum += data[i];
        processingService_->submit([checksum, width, height, timestampNs]() {
            SPDLOG_INFO("CPU job: {}x{}, ts={} ns, cksum={}",
                        width, height, timestampNs, checksum);
        });
    });

    SPDLOG_INFO("Backend initialized.");
    return true;
}

services::SqliteService& AppBackend::sqlite() { return *sqliteService_; }
services::Hdf5Service& AppBackend::hdf5() { return *hdf5Service_; }
services::CaptureService& AppBackend::capture() { return *captureService_; }
services::ProcessingService& AppBackend::processing() { return *processingService_; }
services::PlaybackService& AppBackend::playback() { return *playbackService_; }

} // namespace backend

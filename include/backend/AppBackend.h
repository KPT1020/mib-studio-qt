#pragma once

#include <memory>
#include <string>

namespace backend::services {
class SqliteService;
class Hdf5Service;
class CaptureService;
class ProcessingService;
class PlaybackService;
}

namespace backend { namespace playback { class FrameStore; } }

namespace backend {

class AppBackend {
public:
    AppBackend();
    ~AppBackend();

    bool initialize(const std::string& dataDir);

    services::SqliteService& sqlite();
    services::Hdf5Service& hdf5();
    services::CaptureService& capture();
    services::ProcessingService& processing();
    services::PlaybackService& playback();

private:
    std::unique_ptr<services::SqliteService> sqliteService_;
    std::unique_ptr<services::Hdf5Service> hdf5Service_;
    std::unique_ptr<services::CaptureService> captureService_;
    std::unique_ptr<services::ProcessingService> processingService_;
    std::unique_ptr<services::PlaybackService> playbackService_;
    std::shared_ptr<playback::FrameStore> frameStore_;
};

} // namespace backend

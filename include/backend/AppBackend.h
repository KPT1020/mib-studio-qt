#pragma once

#include <memory>
#include <string>

namespace backend::services {
class SqliteService;
class Hdf5Service;
class CaptureService;
class ProcessingService;
}

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

private:
    std::unique_ptr<services::SqliteService> sqliteService_;
    std::unique_ptr<services::Hdf5Service> hdf5Service_;
    std::unique_ptr<services::CaptureService> captureService_;
    std::unique_ptr<services::ProcessingService> processingService_;
};

} // namespace backend

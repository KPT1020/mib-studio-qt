#include "backend/recording/RecorderService.h"

#include <spdlog/spdlog.h>

namespace backend::services {

struct RecorderService::Impl { bool opened = false; };

RecorderService::RecorderService() : impl_(std::make_unique<Impl>()) {}
RecorderService::~RecorderService() { close(); }

bool RecorderService::openForWrite(const std::string& containerDir) {
    (void)containerDir;
    SPDLOG_WARN("Legacy RecorderService raw writer is unavailable; AppBackend frame recording uses HDF5");
    impl_->opened = false;
    return false;
}

bool RecorderService::writeFrame(const void* data,
                                 size_t size,
                                 size_t pitch,
                                 size_t width,
                                 size_t height,
                                 uint64_t pixelFormat,
                                 size_t partCount,
                                 size_t partSize,
                                 uint64_t timestampNs,
                                 uint64_t userData) {
    (void)data; (void)size; (void)pitch; (void)width; (void)height; (void)pixelFormat; (void)partCount; (void)partSize; (void)timestampNs; (void)userData;
    return false;
}

void RecorderService::close() { impl_->opened = false; }

bool RecorderService::isOpen() const { return impl_->opened; }

} // namespace backend::services

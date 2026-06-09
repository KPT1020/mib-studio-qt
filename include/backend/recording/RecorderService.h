#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace backend::services {

// Legacy raw-frame container writer kept for source compatibility. The main
// frame-recording path is AppBackend::startFrameRecording, backed by Hdf5Service.
class RecorderService {
public:
    RecorderService();
    ~RecorderService();

    bool openForWrite(const std::string& containerDir);
    bool writeFrame(const void* data,
                    size_t size,
                    size_t pitch,
                    size_t width,
                    size_t height,
                    uint64_t pixelFormat,
                    size_t partCount,
                    size_t partSize,
                    uint64_t timestampNs,
                    uint64_t userData = 0);
    void close();

    bool isOpen() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace backend::services

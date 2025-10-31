#include "backend/services/CaptureService.h"

#include <EGrabber.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <stdexcept>

using namespace Euresys;

namespace backend::services {

CaptureService::CaptureService() = default;
CaptureService::~CaptureService() { stop(); }

void CaptureService::setConfig(const Config& cfg) { config_ = cfg; }

void CaptureService::setFrameCallback(FrameCallback cb) { callback_ = std::move(cb); }

bool CaptureService::start() {
    if (running_.load()) return true;
    running_.store(true);
    thread_ = std::thread(&CaptureService::run, this);
    return true;
}

void CaptureService::stop() {
    if (!running_.load()) return;
    running_.store(false);
    if (thread_.joinable()) thread_.join();
}

bool CaptureService::isRunning() const { return running_.load(); }

void CaptureService::run() {
    auto nowUs = []() -> uint64_t {
        using namespace std::chrono;
        return static_cast<uint64_t>(duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
    };

    try {
        SPDLOG_INFO("CaptureService starting: parts={}, buffers={}", config_.bufferPartCount, config_.numBuffers);

        EGenTL genTL;
        EGrabber<CallbackOnDemand> grabber(genTL);

        // Determine width/height first
        grabber.setInteger<StreamModule>("BufferPartCount", 1);
        uint64_t width = grabber.getInteger<StreamModule>("Width");
        uint64_t height = grabber.getInteger<StreamModule>("Height");

        // Configure buffer parts and allocate buffers
        grabber.setInteger<StreamModule>("BufferPartCount", config_.bufferPartCount);
        BufferIndexRange bir = grabber.reallocBuffers(config_.numBuffers);

        size_t offset = 0;
        grabber.start();

        uint64_t tShowStats = nowUs() + 1000000ULL;

        while (running_.load()) {
            // Access next buffer in round-robin
            size_t bufferIndex = bir.indexAt(offset);
            uint8_t* bufferPtr = grabber.getBufferInfo<uint8_t*>(bufferIndex, gc::BUFFER_INFO_BASE);
            size_t imageSize = grabber.getBufferInfo<size_t>(bufferIndex, ge::BUFFER_INFO_CUSTOM_PART_SIZE);
            size_t imageCount = grabber.getBufferInfo<size_t>(bufferIndex, ge::BUFFER_INFO_CUSTOM_NUM_PARTS);

            // Process delivered parts as they become available
            size_t processed = 0;
            size_t delivered = 0;
            do {
                delivered = grabber.getBufferInfo<size_t>(bufferIndex, ge::BUFFER_INFO_CUSTOM_NUM_DELIVERED_PARTS);
                while (processed < delivered) {
                    const uint8_t* imagePtr = bufferPtr + processed * imageSize;
                    uint64_t timestamp = grabber.getBufferInfo<uint64_t>(bufferIndex, gc::BUFFER_INFO_TIMESTAMP);

                    if (callback_) {
                        callback_(imagePtr, imageSize, width, height, timestamp);
                    }
                    stats_.framesProcessed.fetch_add(1, std::memory_order_relaxed);
                    ++processed;
                }
                if (!running_.load()) break;
            } while (delivered < imageCount);

            // Pop current ready buffer and push it back into the input FIFO
            ScopedBuffer buffer(grabber);
            offset = (offset + 1) % bir.size();

            // Periodic stats update
            uint64_t now = nowUs();
            if (now >= tShowStats) {
                uint64_t fr = grabber.getInteger<StreamModule>("StatisticsFrameRate");
                uint64_t dr = grabber.getInteger<StreamModule>("StatisticsDataRate");
                stats_.lastFrameRate.store(fr, std::memory_order_relaxed);
                stats_.lastDataRateMBps.store(dr, std::memory_order_relaxed);
                SPDLOG_INFO("Capture stats: {}x{}, {} MB/s, {} fps", width, height, dr, fr);
                tShowStats += 1000000ULL;
            }
        }

        grabber.stop();
        SPDLOG_INFO("CaptureService stopped");
    } catch (const std::exception& ex) {
        SPDLOG_ERROR("CaptureService exception: {}", ex.what());
        running_.store(false);
    } catch (...) {
        SPDLOG_ERROR("CaptureService unknown exception");
        running_.store(false);
    }
}

} // namespace backend::services

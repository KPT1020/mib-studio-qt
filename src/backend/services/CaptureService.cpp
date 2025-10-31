#include "backend/services/CaptureService.h"
#include "backend/Tools.h"
#include "backend/playback/FrameStore.h"

#include <EGrabber.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace Euresys;

namespace backend::services {

CaptureService::CaptureService() = default;
CaptureService::~CaptureService() { stop(); }

void CaptureService::setConfig(const Config& cfg) { config_ = cfg; }

void CaptureService::setFrameCallback(FrameCallback cb) { callback_ = std::move(cb); }

void CaptureService::setFrameStore(std::shared_ptr<backend::playback::FrameStore> store) { frameStore_ = std::move(store); }

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

    try {
        SPDLOG_INFO("CaptureService starting: parts={}, buffers={}", config_.bufferPartCount, config_.numBuffers);

        EGenTL genTL;
        EGrabber<CallbackOnDemand> grabber(genTL);

        // Determine width/height first
        grabber.setInteger<StreamModule>("BufferPartCount", 1);
        uint64_t width = grabber.getInteger<StreamModule>("Width");
        uint64_t height = grabber.getInteger<StreamModule>("Height");

        // Configure buffer parts and allocate buffers - following 310-high-frame-rate.cpp pattern
        grabber.setInteger<StreamModule>("BufferPartCount", config_.bufferPartCount);
        grabber.reallocBuffers(config_.numBuffers);

        grabber.start();

        // Give camera time to start streaming
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        uint64_t tShowStats = Tools::getTimestamp() + 1000000ULL;

        while (running_.load()) {
            // Use ScopedBuffer to get next available buffer - matches 310-high-frame-rate.cpp
            ScopedBuffer buffer(grabber);
            uint8_t* bufferPtr = buffer.getInfo<uint8_t*>(gc::BUFFER_INFO_BASE);
            size_t imageSize = buffer.getInfo<size_t>(ge::BUFFER_INFO_CUSTOM_PART_SIZE);
            size_t linePitch = buffer.getInfo<size_t>(ge::BUFFER_INFO_CUSTOM_LINE_PITCH);
            uint64_t pixelFormat = buffer.getInfo<uint64_t>(gc::BUFFER_INFO_PIXELFORMAT);

            // Process available images - matches 310-high-frame-rate.cpp
            size_t delivered = buffer.getInfo<size_t>(ge::BUFFER_INFO_CUSTOM_NUM_DELIVERED_PARTS);
            size_t processed = 0;

            // Get individual part timestamps for high frame rate accuracy
            std::vector<char> tsData = buffer.getInfo<std::vector<char>>(ge::BUFFER_INFO_CUSTOM_PART_TIMESTAMPS);
            const size_t tsCount = tsData.size() / sizeof(uint64_t);
            uint64_t* timestamps = tsCount > 0 ? reinterpret_cast<uint64_t*>(&tsData[0]) : nullptr;

            while (processed < delivered) {
                const uint8_t* imagePtr = bufferPtr + processed * imageSize;
                // Use individual part timestamp if available, otherwise use buffer timestamp
                uint64_t timestamp = (timestamps && processed < tsCount) ? timestamps[processed] :
                                   buffer.getInfo<uint64_t>(gc::BUFFER_INFO_TIMESTAMP);

                if (callback_) {
                    callback_(imagePtr, imageSize, width, height, timestamp);
                }
                if (frameStore_) {
                    frameStore_->pushFrame(imagePtr,
                                           imageSize,
                                           width,
                                           height,
                                           linePitch,
                                           pixelFormat,
                                           timestamp);
                }
                stats_.framesProcessed.fetch_add(1, std::memory_order_relaxed);
                ++processed;
            }

            // Periodic stats update
            uint64_t now = Tools::getTimestamp();
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

#include "backend/services/ProcessingService.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <numeric>
#include <set>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace {

using backend::services::ProcessedFrame;
using backend::services::ProcessingConfig;
using backend::services::ProcessingService;

cv::Mat makeRingFrame(int width = 96, int height = 96)
{
    cv::Mat image(height, width, CV_8UC1, cv::Scalar(0));
    cv::circle(image, cv::Point(width / 2, height / 2), 22, cv::Scalar(220), -1, cv::LINE_AA);
    cv::circle(image, cv::Point(width / 2, height / 2), 10, cv::Scalar(0), -1, cv::LINE_AA);
    return image;
}

ProcessingConfig permissiveConfig()
{
    ProcessingConfig config;
    config.gaussian_blur_size = 3;
    config.bg_subtract_threshold = 8;
    config.morph_kernel_size = 3;
    config.morph_iterations = 1;
    config.enable_area_range_check = false;
    config.enable_deformability_range_check = false;
    config.enable_ring_ratio_check = false;
    config.enable_border_check = false;
    config.require_single_inner_contour = false;
    return config;
}

bool waitFor(std::condition_variable& cv,
             std::mutex& mutex,
             const std::function<bool()>& predicate,
             std::chrono::seconds timeout = std::chrono::seconds(5))
{
    std::unique_lock<std::mutex> lk(mutex);
    return cv.wait_for(lk, timeout, predicate);
}

bool hasMetrics(const std::vector<ProcessedFrame>& frames)
{
    for (const ProcessedFrame& frame : frames) {
        if (frame.validation.area > 0.0 &&
            frame.validation.deformability >= 0.0 &&
            frame.validation.ringRatio > 0.0 &&
            frame.validation.innerContourCount > 0) {
            return true;
        }
    }
    return false;
}

int testNonBlockingBoundedQueue()
{
    ProcessingService service;
    cv::Mat frame = makeRingFrame();

    std::mutex resultMutex;
    std::condition_variable resultCv;
    std::vector<ProcessedFrame> results;
    bool firstCallbackEntered = false;
    bool releaseFirstCallback = false;

    service.setBatchResultCallback([&](std::vector<ProcessedFrame>&& batch) {
        std::unique_lock<std::mutex> lk(resultMutex);
        firstCallbackEntered = true;
        resultCv.notify_all();
        resultCv.wait(lk, [&] { return releaseFirstCallback; });
        for (auto& processed : batch) {
            results.emplace_back(std::move(processed));
        }
        lk.unlock();
        resultCv.notify_all();
    });

    ProcessingService::BatchPipelineConfig config;
    config.batchSize = 1;
    config.workerCount = 1;
    config.maxQueuedFrames = 1;
    config.processingConfig = permissiveConfig();
    service.startBatchPipeline(config);

    auto releaseAndStop = [&] {
        {
            std::lock_guard<std::mutex> lk(resultMutex);
            releaseFirstCallback = true;
        }
        resultCv.notify_all();
        service.stopBatchPipeline();
    };

    const auto payloadSize = static_cast<size_t>(frame.step) * static_cast<size_t>(frame.rows);
    if (!service.enqueueBatchFrame(frame.data,
                                   payloadSize,
                                   static_cast<uint64_t>(frame.cols),
                                   static_cast<uint64_t>(frame.rows),
                                   static_cast<size_t>(frame.step),
                                   0,
                                   100,
                                   10)) {
        releaseAndStop();
        return 11;
    }

    if (!waitFor(resultCv, resultMutex, [&] { return firstCallbackEntered; })) {
        releaseAndStop();
        return 12;
    }

    if (!service.enqueueBatchFrame(frame.data,
                                   payloadSize,
                                   static_cast<uint64_t>(frame.cols),
                                   static_cast<uint64_t>(frame.rows),
                                   static_cast<size_t>(frame.step),
                                   0,
                                   200,
                                   11)) {
        releaseAndStop();
        return 13;
    }

    const auto enqueueStart = std::chrono::steady_clock::now();
    const bool thirdQueued = service.enqueueBatchFrame(frame.data,
                                                       payloadSize,
                                                       static_cast<uint64_t>(frame.cols),
                                                       static_cast<uint64_t>(frame.rows),
                                                       static_cast<size_t>(frame.step),
                                                       0,
                                                       300,
                                                       12);
    const auto enqueueElapsed = std::chrono::steady_clock::now() - enqueueStart;
    if (thirdQueued) {
        releaseAndStop();
        return 14;
    }
    if (enqueueElapsed > std::chrono::milliseconds(250)) {
        releaseAndStop();
        return 15;
    }

    {
        std::lock_guard<std::mutex> lk(resultMutex);
        releaseFirstCallback = true;
    }
    resultCv.notify_all();

    if (!waitFor(resultCv, resultMutex, [&] { return results.size() >= 2; })) {
        releaseAndStop();
        return 16;
    }

    service.stopBatchPipeline();

    const auto stats = service.getBatchPipelineStats();
    if (stats.framesEnqueued != 2 || stats.framesDropped != 1 || stats.framesProcessed != 2) {
        return 17;
    }
    if (stats.running || stats.batchesProcessed == 0 || !hasMetrics(results)) {
        return 18;
    }

    return 0;
}

int testConfigAndLargeBatch()
{
    constexpr size_t kFrameCount = 5000;

    ProcessingService service;
    cv::Mat frame = makeRingFrame(64, 64);
    const auto payloadSize = static_cast<size_t>(frame.step) * static_cast<size_t>(frame.rows);

    std::mutex resultMutex;
    std::condition_variable resultCv;
    std::vector<uint64_t> resultIndices;
    std::vector<size_t> callbackBatchSizes;
    bool sawMetrics = false;

    service.setBatchResultCallback([&](std::vector<ProcessedFrame>&& batch) {
        std::lock_guard<std::mutex> lk(resultMutex);
        callbackBatchSizes.push_back(batch.size());
        for (const ProcessedFrame& processed : batch) {
            resultIndices.push_back(processed.index);
            if (processed.validation.area > 0.0 &&
                processed.validation.deformability >= 0.0 &&
                processed.validation.ringRatio > 0.0) {
                sawMetrics = true;
            }
        }
        resultCv.notify_all();
    });

    ProcessingService::BatchPipelineConfig config;
    config.batchSize = kFrameCount;
    config.workerCount = 2;
    config.maxQueuedFrames = kFrameCount;
    config.maxBatchWaitMs = 2000;
    config.processingConfig = permissiveConfig();
    service.startBatchPipeline(config);

    const auto normalized = service.getBatchPipelineConfig();
    if (normalized.batchSize != kFrameCount ||
        normalized.workerCount != 2 ||
        normalized.maxQueuedFrames != kFrameCount ||
        normalized.maxBatchWaitMs != 2000) {
        service.stopBatchPipeline();
        return 21;
    }

    size_t accepted = 0;
    const auto enqueueStart = std::chrono::steady_clock::now();
    for (size_t i = 0; i < kFrameCount; ++i) {
        const bool queued = service.enqueueBatchFrame(frame.data,
                                                      payloadSize,
                                                      static_cast<uint64_t>(frame.cols),
                                                      static_cast<uint64_t>(frame.rows),
                                                      static_cast<size_t>(frame.step),
                                                      0,
                                                      static_cast<uint64_t>(1000 + i),
                                                      static_cast<uint64_t>(i));
        if (queued) {
            ++accepted;
        }
    }
    const auto enqueueElapsed = std::chrono::steady_clock::now() - enqueueStart;

    if (accepted != kFrameCount) {
        service.stopBatchPipeline();
        return 22;
    }
    if (enqueueElapsed > std::chrono::seconds(2)) {
        service.stopBatchPipeline();
        return 23;
    }

    if (!waitFor(resultCv,
                 resultMutex,
                 [&] { return resultIndices.size() >= kFrameCount; },
                 std::chrono::seconds(15))) {
        service.stopBatchPipeline();
        return 24;
    }

    service.stopBatchPipeline();

    const auto stats = service.getBatchPipelineStats();
    if (stats.framesEnqueued != kFrameCount ||
        stats.framesDropped != 0 ||
        stats.framesProcessed != kFrameCount ||
        stats.queuedFrames != 0 ||
        stats.running) {
        return 25;
    }

    if (callbackBatchSizes.empty() || callbackBatchSizes.front() != kFrameCount) {
        return 26;
    }

    const std::set<uint64_t> uniqueIndices(resultIndices.begin(), resultIndices.end());
    if (uniqueIndices.size() != kFrameCount ||
        *uniqueIndices.begin() != 0 ||
        *uniqueIndices.rbegin() != kFrameCount - 1) {
        return 27;
    }

    if (!sawMetrics) {
        return 28;
    }

    return 0;
}

} // namespace

int main()
{
    if (const int rc = testNonBlockingBoundedQueue(); rc != 0) {
        return rc;
    }
    if (const int rc = testConfigAndLargeBatch(); rc != 0) {
        return rc;
    }
    return 0;
}

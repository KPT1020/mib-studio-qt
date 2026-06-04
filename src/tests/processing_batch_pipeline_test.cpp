#include "backend/services/ProcessingService.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace
{
using backend::services::ProcessedFrame;
using backend::services::ProcessingService;

cv::Mat makeRingFrame()
{
    cv::Mat frame(96, 96, CV_8UC1, cv::Scalar(0));
    cv::circle(frame, cv::Point(48, 48), 20, cv::Scalar(255), cv::FILLED);
    cv::circle(frame, cv::Point(48, 48), 10, cv::Scalar(0), cv::FILLED);
    return frame;
}

bool waitFor(std::condition_variable& cv,
             std::mutex& mutex,
             const std::function<bool()>& predicate)
{
    std::unique_lock<std::mutex> lk(mutex);
    return cv.wait_for(lk, std::chrono::seconds(5), predicate);
}
} // namespace

int main()
{
    ProcessingService service;

    ProcessingService::BatchPipelineConfig batchConfig;
    batchConfig.batchSize = 1;
    batchConfig.workerCount = 1;
    batchConfig.maxQueuedFrames = 1;
    batchConfig.processingConfig.enable_area_range_check = false;
    batchConfig.processingConfig.enable_ring_ratio_check = false;
    batchConfig.processingConfig.enable_deformability_range_check = false;

    std::mutex resultMutex;
    std::condition_variable resultCv;
    std::vector<ProcessedFrame> results;
    bool firstCallbackEntered = false;
    bool releaseFirstCallback = false;
    size_t callbackCount = 0;

    service.setBatchResultCallback([&](std::vector<ProcessedFrame>&& batch) {
        std::unique_lock<std::mutex> lk(resultMutex);
        ++callbackCount;
        for (auto& frame : batch) {
            results.emplace_back(std::move(frame));
        }
        if (callbackCount == 1) {
            firstCallbackEntered = true;
            resultCv.notify_all();
            resultCv.wait(lk, [&] { return releaseFirstCallback; });
        }
        resultCv.notify_all();
    });

    service.startBatchPipeline(batchConfig);

    auto releaseAndStop = [&]() {
        {
            std::lock_guard<std::mutex> lk(resultMutex);
            releaseFirstCallback = true;
        }
        resultCv.notify_all();
        service.stopBatchPipeline();
    };

    const cv::Mat frame = makeRingFrame();
    if (!service.enqueueBatchFrame(frame.data,
                                   frame.total(),
                                   static_cast<uint64_t>(frame.cols),
                                   static_cast<uint64_t>(frame.rows),
                                   static_cast<size_t>(frame.step),
                                   0,
                                   100,
                                   10)) {
        releaseAndStop();
        return 1;
    }

    if (!waitFor(resultCv, resultMutex, [&] { return firstCallbackEntered; })) {
        releaseAndStop();
        return 2;
    }

    if (!service.enqueueBatchFrame(frame.data,
                                   frame.total(),
                                   static_cast<uint64_t>(frame.cols),
                                   static_cast<uint64_t>(frame.rows),
                                   static_cast<size_t>(frame.step),
                                   0,
                                   200,
                                   11)) {
        releaseAndStop();
        return 3;
    }

    const auto enqueueStart = std::chrono::steady_clock::now();
    const bool thirdQueued = service.enqueueBatchFrame(frame.data,
                                                       frame.total(),
                                                       static_cast<uint64_t>(frame.cols),
                                                       static_cast<uint64_t>(frame.rows),
                                                       static_cast<size_t>(frame.step),
                                                       0,
                                                       300,
                                                       12);
    const auto enqueueElapsed = std::chrono::steady_clock::now() - enqueueStart;
    if (thirdQueued) {
        releaseAndStop();
        return 4;
    }
    if (enqueueElapsed > std::chrono::milliseconds(250)) {
        releaseAndStop();
        return 5;
    }

    {
        std::lock_guard<std::mutex> lk(resultMutex);
        releaseFirstCallback = true;
    }
    resultCv.notify_all();

    if (!waitFor(resultCv, resultMutex, [&] { return results.size() >= 2; })) {
        releaseAndStop();
        return 6;
    }

    service.stopBatchPipeline();

    const auto stats = service.getBatchPipelineStats();
    if (stats.framesEnqueued != 2 || stats.framesDropped != 1 || stats.framesProcessed != 2) {
        return 7;
    }
    if (stats.running || stats.batchesProcessed == 0) {
        return 8;
    }

    bool sawMetrics = false;
    for (const ProcessedFrame& processed : results) {
        if (processed.validation.area > 0.0 &&
            processed.validation.deformability >= 0.0 &&
            processed.validation.ringRatio > 0.0 &&
            processed.validation.innerContourCount > 0) {
            sawMetrics = true;
            break;
        }
    }
    if (!sawMetrics) {
        return 9;
    }

    return 0;
}

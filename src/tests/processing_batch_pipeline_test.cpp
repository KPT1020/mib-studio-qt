#include "backend/services/ProcessingService.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <opencv2/imgproc.hpp>

namespace {

using backend::services::ProcessedFrame;
using backend::services::ProcessingConfig;
using backend::services::ProcessingService;

cv::Mat makeRingFrame(int width = 96, int height = 96)
{
    cv::Mat image(height, width, CV_8UC1, cv::Scalar(0));
    const cv::Point center(width / 2, height / 2);
    cv::circle(image, center, 24, cv::Scalar(255), -1);
    cv::circle(image, center, 11, cv::Scalar(0), -1);
    return image;
}

cv::Mat makeTwoRingObjects()
{
    cv::Mat image(96, 160, CV_8UC1, cv::Scalar(0));
    const std::vector<cv::Point> centers{{48, 48}, {112, 48}};
    for (const auto& center : centers) {
        cv::circle(image, center, 22, cv::Scalar(255), cv::FILLED);
        cv::circle(image, center, 11, cv::Scalar(0), cv::FILLED);
    }
    return image;
}

ProcessingConfig makeTestConfig()
{
    ProcessingConfig config;
    config.gaussian_blur_size = 3;
    config.bg_subtract_threshold = 20;
    config.morph_kernel_size = 3;
    config.morph_iterations = 1;
    config.enable_area_range_check = false;
    config.enable_deformability_range_check = false;
    config.enable_ring_ratio_check = false;
    config.enable_area_ratio_check = false;
    config.require_single_inner_contour = true;
    config.empty_frame_pixel_threshold = 1;
    return config;
}

bool waitFor(const std::function<bool()>& predicate, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

bool require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

bool testCaptureLoopCanEnqueueWhileWorkerIsBlocked()
{
    ProcessingService service;
    ProcessingService::BatchPipelineConfig config;
    config.batchSize = 1;
    config.maxQueuedFrames = 128;
    config.workerCount = 1;
    config.processing = makeTestConfig();

    std::mutex mutex;
    std::condition_variable condition;
    bool callbackEntered = false;
    bool releaseCallback = false;
    size_t emittedFrames = 0;

    const bool started = service.startBatchPipeline(config, [&](std::vector<ProcessedFrame> batch) {
        std::unique_lock<std::mutex> lock(mutex);
        callbackEntered = true;
        condition.notify_all();
        condition.wait(lock, [&] { return releaseCallback; });
        emittedFrames += batch.size();
    });
    if (!require(started, "batch pipeline should start")) {
        return false;
    }

    const cv::Mat frame = makeRingFrame();
    if (!require(service.enqueueBatchFrame(frame, 0, 0), "first frame should enqueue")) {
        service.stopBatchPipeline();
        return false;
    }

    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!condition.wait_for(lock, std::chrono::seconds(2), [&] { return callbackEntered; })) {
            std::cerr << "worker callback did not enter blocked section\n";
            releaseCallback = true;
            lock.unlock();
            condition.notify_all();
            service.stopBatchPipeline();
            return false;
        }
    }

    size_t acceptedWhileBlocked = 0;
    for (uint64_t i = 1; i <= 100; ++i) {
        if (service.enqueueBatchFrame(frame, i, i * 1000)) {
            ++acceptedWhileBlocked;
        }
    }

    const auto statsWhileBlocked = service.getBatchPipelineStats();
    bool ok = true;
    ok &= require(acceptedWhileBlocked == 100, "capture loop should enqueue 100 frames while worker callback is blocked");
    ok &= require(statsWhileBlocked.framesAccepted == 101, "accepted frame count should include blocked-worker enqueue burst");
    ok &= require(statsWhileBlocked.framesDropped == 0, "non-full queue should not drop frames during blocked-worker enqueue burst");
    ok &= require(statsWhileBlocked.currentQueueDepth == 100, "queued depth should hold frames while worker is blocked");

    {
        std::scoped_lock lock(mutex);
        releaseCallback = true;
    }
    condition.notify_all();
    service.stopBatchPipeline();

    ok &= require(emittedFrames == 101, "stop should drain queued frames through callback");
    return ok;
}

bool testConfigurableBatchSizeAndConcurrency()
{
    ProcessingService service;
    ProcessingService::BatchPipelineConfig config;
    config.batchSize = 4;
    config.maxQueuedFrames = 16;
    config.workerCount = 2;
    config.processing = makeTestConfig();

    std::mutex mutex;
    std::condition_variable condition;
    std::vector<size_t> batchSizes;
    size_t processed = 0;

    const bool started = service.startBatchPipeline(config, [&](std::vector<ProcessedFrame> batch) {
        std::scoped_lock lock(mutex);
        processed += batch.size();
        batchSizes.push_back(batch.size());
        condition.notify_all();
    });
    if (!require(started, "batch pipeline should start for configurable batch test")) {
        return false;
    }

    const cv::Mat frame = makeRingFrame();
    for (uint64_t i = 0; i < 8; ++i) {
        if (!require(service.enqueueBatchFrame(frame, i, i), "frame should enqueue for configured batch test")) {
            service.stopBatchPipeline();
            return false;
        }
    }

    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!condition.wait_for(lock, std::chrono::seconds(5), [&] { return processed == 8; })) {
            std::cerr << "configured batch test timed out waiting for 8 processed frames\n";
            service.stopBatchPipeline();
            return false;
        }
    }

    const auto stats = service.getBatchPipelineStats();
    service.stopBatchPipeline();

    bool ok = true;
    ok &= require(batchSizes.size() == 2, "two 4-frame batches should be emitted");
    for (const size_t size : batchSizes) {
        ok &= require(size == 4, "each emitted batch should match configured batch size");
    }
    ok &= require(stats.batchSize == 4, "stats should report configured batch size");
    ok &= require(stats.workerCount == 2, "stats should report configured worker count");
    ok &= require(stats.framesProcessed == 8, "stats should report all frames processed");
    return ok;
}

bool testBoundedQueueDropsWithoutBlocking()
{
    ProcessingService service;
    ProcessingService::BatchPipelineConfig config;
    config.batchSize = 8;
    config.maxQueuedFrames = 3;
    config.workerCount = 1;
    config.processing = makeTestConfig();

    const bool started = service.startBatchPipeline(config, {});
    if (!require(started, "batch pipeline should start for bounded queue test")) {
        return false;
    }

    const cv::Mat frame = makeRingFrame();
    const bool first = service.enqueueBatchFrame(frame, 0, 0);
    const bool second = service.enqueueBatchFrame(frame, 1, 0);
    const bool third = service.enqueueBatchFrame(frame, 2, 0);
    const bool fourth = service.enqueueBatchFrame(frame, 3, 0);
    const auto stats = service.getBatchPipelineStats();
    service.stopBatchPipeline();

    bool ok = true;
    ok &= require(first && second && third, "bounded queue should accept frames until capacity");
    ok &= require(!fourth, "bounded queue should reject frame beyond capacity without blocking");
    ok &= require(stats.framesAccepted == 3, "bounded queue accepted count should be 3");
    ok &= require(stats.framesDropped == 1, "bounded queue dropped count should be 1");
    ok &= require(stats.currentQueueDepth == 3, "bounded queue should retain queued frames until stop drains residuals");
    return ok;
}

bool testMetricsAreEmittedFromBatchPath()
{
    ProcessingService service;
    ProcessingService::BatchPipelineConfig config;
    config.batchSize = 2;
    config.maxQueuedFrames = 4;
    config.workerCount = 1;
    config.processing = makeTestConfig();

    std::mutex mutex;
    std::condition_variable condition;
    std::vector<ProcessedFrame> results;

    const bool started = service.startBatchPipeline(config, [&](std::vector<ProcessedFrame> batch) {
        std::scoped_lock lock(mutex);
        for (auto& frame : batch) {
            results.emplace_back(std::move(frame));
        }
        condition.notify_all();
    });
    if (!require(started, "batch pipeline should start for metrics test")) {
        return false;
    }

    const cv::Mat frame = makeRingFrame();
    if (!require(service.enqueueBatchFrame(frame, 0, 0), "first metrics frame should enqueue") ||
        !require(service.enqueueBatchFrame(frame, 1, 1000), "second metrics frame should enqueue")) {
        service.stopBatchPipeline();
        return false;
    }

    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!condition.wait_for(lock, std::chrono::seconds(5), [&] { return results.size() == 2; })) {
            std::cerr << "metrics test timed out waiting for batch results\n";
            service.stopBatchPipeline();
            return false;
        }
    }

    service.stopBatchPipeline();

    const auto& validation = results.front().validation;
    bool ok = true;
    ok &= require(!results.front().processedImage.empty(), "batch result should include processed mask");
    ok &= require(validation.area > 0.0, "batch result should expose area metric");
    ok &= require(validation.deformability >= 0.0, "batch result should expose deformability metric");
    ok &= require(validation.ringRatio > 0.0, "batch result should expose ring-width/ring-ratio metric");
    return ok;
}

bool testBatchPipelineEmitsMultiObjectRecords()
{
    ProcessingService service;
    ProcessingService::BatchPipelineConfig config;
    config.batchSize = 1;
    config.maxQueuedFrames = 4;
    config.workerCount = 1;
    config.processing = makeTestConfig();

    std::mutex mutex;
    std::condition_variable condition;
    std::vector<ProcessedFrame> results;

    const bool started = service.startBatchPipeline(config, [&](std::vector<ProcessedFrame> batch) {
        std::scoped_lock lock(mutex);
        for (auto& frame : batch) {
            results.emplace_back(std::move(frame));
        }
        condition.notify_all();
    });
    if (!require(started, "batch pipeline should start for multi-object test")) {
        return false;
    }

    const cv::Mat frame = makeTwoRingObjects();
    if (!require(service.enqueueBatchFrame(frame, 7, 7000), "multi-object frame should enqueue")) {
        service.stopBatchPipeline();
        return false;
    }

    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!condition.wait_for(lock, std::chrono::seconds(5), [&] { return results.size() == 2; })) {
            std::cerr << "multi-object batch test timed out waiting for 2 records, got " << results.size() << "\n";
            service.stopBatchPipeline();
            return false;
        }
    }

    service.stopBatchPipeline();

    bool ok = true;
    ok &= require(results.size() == 2, "one two-object frame should emit two records");
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& validation = results[i].validation;
        ok &= require(results[i].index == 7, "multi-object record should preserve source frame index");
        ok &= require(results[i].timestampNs == 7000, "multi-object record should preserve source timestamp");
        ok &= require(validation.objectId == static_cast<int>(i + 1), "multi-object records should have one-based object IDs");
        ok &= require(validation.objectCount == 2, "multi-object records should report total object count");
        ok &= require(validation.isValid, "multi-object records should be valid for permissive test config");
    }
    return ok;
}

} // namespace

int main()
{
    if (!testCaptureLoopCanEnqueueWhileWorkerIsBlocked()) {
        return 1;
    }
    if (!testConfigurableBatchSizeAndConcurrency()) {
        return 2;
    }
    if (!testBoundedQueueDropsWithoutBlocking()) {
        return 3;
    }
    if (!testMetricsAreEmittedFromBatchPath()) {
        return 4;
    }
    if (!testBatchPipelineEmitsMultiObjectRecords()) {
        return 5;
    }
    return 0;
}

#include "backend/services/ProcessingService.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <future>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {

using backend::services::ProcessingConfig;
using backend::services::ProcessingService;

bool expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }
    return true;
}

cv::Mat makeFrame(const std::vector<cv::Point>& centers)
{
    cv::Mat frame(96, 160, CV_8UC1, cv::Scalar(0));
    for (const auto& center : centers)
    {
        cv::circle(frame, center, 18, cv::Scalar(255), -1, cv::LINE_8);
        cv::circle(frame, center, 7, cv::Scalar(0), -1, cv::LINE_8);
    }
    return frame;
}

std::set<uint64_t> trackIdsForFrame(const backend::services::ProcessedFrame& frame)
{
    std::set<uint64_t> ids;
    for (const auto& detection : frame.detections)
    {
        if (detection.trackId != 0)
        {
            ids.insert(detection.trackId);
        }
    }
    return ids;
}

} // namespace

int main()
{
    ProcessingService service;
    service.setPixelToMicronFactor(1.0);

    ProcessingConfig config;
    config.gaussian_blur_size = 1;
    config.bg_subtract_threshold = 127;
    config.morph_kernel_size = 1;
    config.morph_iterations = 1;
    config.empty_frame_pixel_threshold = 20;
    config.enable_area_range_check = true;
    config.area_threshold_min = 50;
    config.area_threshold_max = 400;
    config.enable_ring_ratio_check = false;
    config.enable_deformability_range_check = false;
    config.enable_area_ratio_check = false;
    config.enable_border_check = true;
    config.require_single_inner_contour = true;

    const std::vector<cv::Mat> images = {
        makeFrame({}),
        makeFrame({cv::Point(45, 48)}),
        makeFrame({cv::Point(52, 48), cv::Point(82, 48)}),
        makeFrame({cv::Point(59, 48), cv::Point(89, 48)}),
        makeFrame({}),
    };

    ProcessingService::BatchProcessingOptions options;
    options.workerCount = 2;
    options.maxTrackingDistancePx = 20.0;
    options.maxTrackGapFrames = 1;

    std::vector<ProcessingService::BatchProgress> progressEvents;
    auto future = service.processBatchAsync(
        images,
        config,
        cv::Mat{},
        ProcessingService::Roi{0, 0, 160, 96},
        options,
        [&progressEvents](const ProcessingService::BatchProgress& progress) {
            progressEvents.push_back(progress);
        });

    const auto result = future.get();

    bool ok = true;
    ok &= expect(result.totalInputFrames == 5, "total input frame count is recorded");
    ok &= expect(result.discardedEmptyFrames == 2, "two empty frames are discarded");
    ok &= expect(result.processedFrameCount == 3, "only non-empty frames are retained");
    ok &= expect(result.frames.size() == 3, "retained frame vector size matches processed count");
    ok &= expect(result.detectionCount == 5, "detections are emitted per object, not per frame");
    ok &= expect(result.uniqueObjectCount == 2, "tracking deduplicates repeated detections into two objects");
    ok &= expect(result.tracks.size() == 2, "two track summaries are produced");
    ok &= expect(!progressEvents.empty(), "async batch progress callback fires");
    ok &= expect(progressEvents.front().done == 0 && progressEvents.front().total == 5,
                 "progress begins at 0/total");
    ok &= expect(progressEvents.back().done == 5 && progressEvents.back().total == 5,
                 "progress ends at total/total");

    if (result.frames.size() == 3)
    {
        ok &= expect(result.frames[0].index == 1, "first retained frame keeps source index 1");
        ok &= expect(result.frames[1].index == 2, "second retained frame keeps source index 2");
        ok &= expect(result.frames[2].index == 3, "third retained frame keeps source index 3");
        ok &= expect(result.frames[0].detections.size() == 1, "single-object frame has one detection");
        ok &= expect(result.frames[1].detections.size() == 2, "overlapping frame has two detections");
        ok &= expect(result.frames[2].detections.size() == 2, "shifted overlapping frame has two detections");

        const auto firstIds = trackIdsForFrame(result.frames[0]);
        const auto secondIds = trackIdsForFrame(result.frames[1]);
        const auto thirdIds = trackIdsForFrame(result.frames[2]);
        ok &= expect(firstIds.size() == 1, "single-object frame has one track id");
        ok &= expect(secondIds.size() == 2, "two-object frame has two track ids");
        ok &= expect(thirdIds.size() == 2, "shifted two-object frame keeps two track ids");
        if (!firstIds.empty())
        {
            ok &= expect(secondIds.count(*firstIds.begin()) == 1,
                         "existing object keeps its track when a second object enters");
        }
        ok &= expect(secondIds == thirdIds, "track ids remain stable across shifted frames");
    }

    for (const auto& frame : result.frames)
    {
        ok &= expect(!frame.discardedEmpty, "retained frames are not marked discarded");
        ok &= expect(frame.foregroundPixelCount >= config.empty_frame_pixel_threshold,
                     "retained frames passed foreground threshold");
        for (const auto& detection : frame.detections)
        {
            ok &= expect(detection.isValid, "synthetic detection is valid");
            ok &= expect(detection.trackId != 0, "tracked detection has track id");
        }
    }

    return ok ? 0 : 1;
}

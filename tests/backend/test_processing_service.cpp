#include <catch2/catch_test_macros.hpp>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include "backend/services/ProcessingService.h"

using backend::services::ProcessedFrame;
using backend::services::ProcessingConfig;
using backend::services::ProcessingService;

static ProcessingConfig makeTestConfig()
{
    ProcessingConfig cfg;
    cfg.bg_subtract_threshold = 1;
    cfg.gaussian_blur_size = 3;
    cfg.morph_kernel_size = 3;
    cfg.morph_iterations = 1;
    cfg.enable_border_check = true;
    cfg.enable_area_range_check = true;
    cfg.area_threshold_min = 0;
    cfg.area_threshold_max = 1000000;
    cfg.enable_ring_ratio_check = false;
    cfg.enable_deformability_range_check = false;
    cfg.enable_area_ratio_check = false;
    cfg.require_single_inner_contour = false;
    return cfg;
}

TEST_CASE("ProcessingService::computeProcessedFrame coerces input and respects ROI", "[ProcessingService]")
{
    ProcessingService svc;
    const ProcessingConfig cfg = makeTestConfig();

    cv::Mat bgr(60, 60, CV_8UC3, cv::Scalar(0, 0, 0));
    cv::circle(bgr, cv::Point(30, 30), 13, cv::Scalar(255, 255, 255), -1);

    const ProcessingService::Roi roi{10, 10, 40, 40};
    ProcessedFrame out = svc.computeProcessedFrame(bgr, cv::Mat{}, cfg, roi, 123, 456);

    REQUIRE(out.index == 123);
    REQUIRE(out.timestampNs == 456);

    REQUIRE(!out.originalImage.empty());
    CHECK(out.originalImage.type() == CV_8UC1);
    CHECK(out.originalImage.rows == 60);
    CHECK(out.originalImage.cols == 60);

    REQUIRE(!out.processedImage.empty());
    CHECK(out.processedImage.type() == CV_8UC1);
    CHECK(out.processedImage.rows == 60);
    CHECK(out.processedImage.cols == 60);

    cv::Mat outside = out.processedImage.clone();
    outside(cv::Rect(roi.x, roi.y, roi.w, roi.h)).setTo(0);
    CHECK(cv::countNonZero(outside) == 0);
}

TEST_CASE("ProcessingService::processBatch returns per-image frames and reports progress", "[ProcessingService]")
{
    ProcessingService svc;
    const ProcessingConfig cfg = makeTestConfig();

    std::vector<cv::Mat> images;
    images.emplace_back(20, 20, CV_8UC1, cv::Scalar(0));
    images.emplace_back(20, 20, CV_8UC1, cv::Scalar(0));
    images.emplace_back(20, 20, CV_8UC1, cv::Scalar(0));

    cv::circle(images[0], cv::Point(10, 10), 6, cv::Scalar(255), -1);
    cv::circle(images[1], cv::Point(10, 10), 6, cv::Scalar(255), -1);
    cv::circle(images[2], cv::Point(10, 10), 6, cv::Scalar(255), -1);

    std::vector<size_t> doneValues;
    std::vector<size_t> totalValues;

    auto progress = [&](const ProcessingService::BatchProgress& p) {
        doneValues.push_back(p.done);
        totalValues.push_back(p.total);
    };

    const auto roi = ProcessingService::Roi{0, 0, 0, 0};
    std::vector<ProcessedFrame> out = svc.processBatch(images, cfg, cv::Mat{}, roi, progress);

    REQUIRE(out.size() == images.size());
    CHECK(out[0].index == 0);
    CHECK(out[1].index == 1);
    CHECK(out[2].index == 2);

    REQUIRE(!doneValues.empty());
    REQUIRE(doneValues.front() == 0);
    REQUIRE(doneValues.back() == images.size());
    REQUIRE(totalValues.front() == images.size());
    REQUIRE(totalValues.back() == images.size());

    for (size_t i = 1; i < doneValues.size(); ++i) {
        CHECK(doneValues[i] >= doneValues[i - 1]);
    }
}

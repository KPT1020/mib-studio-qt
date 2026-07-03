// Proposed pipeline (config.proposed_pipeline) — deterministic invariant that
// does not need the GT dataset (the dataset A/B lives in
// processing_proposed_pipeline_bench).
//
// Contract: the accuracy win is cv::absdiff replacing signed cv::subtract, which
// keeps the part of the cell that is DARKER than the background. Signed subtract
// clips that to zero and loses it. A cell that is brighter than bg on one half
// and darker on the other therefore segments to (roughly) half its footprint
// under the current pipeline and its whole footprint under the proposed one.

#include "backend/processing/ProcessingService.h"

#include <opencv2/imgproc.hpp>

#include <iostream>

using backend::services::ProcessingConfig;
using backend::services::ProcessingService;
using Roi = ProcessingService::Roi;

namespace {

ProcessingConfig baseConfig() {
    ProcessingConfig c;
    c.gaussian_blur_size = 1;   // identity blur
    c.morph_kernel_size = 1;    // identity morphology — mask == threshold
    c.morph_iterations = 1;
    c.bg_subtract_threshold = 20;
    c.enable_border_check = false;
    c.enable_area_range_check = false;
    c.enable_deformability_range_check = false;
    c.enable_ring_ratio_check = false;
    c.enable_area_ratio_check = false;
    c.require_single_inner_contour = false;
    c.empty_frame_pixel_threshold = 1;
    return c;
}

int maskPixels(const ProcessingConfig& c, const cv::Mat& frame, const cv::Mat& bg) {
    auto pf = ProcessingService{}.computeProcessedFrame(frame, bg, c, Roi{0, 0, 0, 0}, 0, 0);
    return cv::countNonZero(pf.processedImage);
}

} // namespace

int main() {
    // Uniform background = 100. A disk that is brighter than bg on its left half
    // (150) and darker on its right half (50) — both deviate by 50.
    const int bgVal = 100;
    cv::Mat bg(120, 120, CV_8UC1, cv::Scalar(bgVal));
    cv::Mat frame = bg.clone();
    cv::Mat disk(120, 120, CV_8UC1, cv::Scalar(0));
    cv::circle(disk, {60, 60}, 20, cv::Scalar(255), cv::FILLED);
    for (int y = 0; y < frame.rows; ++y) {
        for (int x = 0; x < frame.cols; ++x) {
            if (disk.at<uchar>(y, x)) {
                frame.at<uchar>(y, x) = (x < 60) ? 150 : 50;  // bright | dark
            }
        }
    }

    // Current pipeline: signed subtract keeps only the brighter-than-bg half.
    ProcessingConfig current = baseConfig();
    current.proposed_pipeline = false;
    const int curPx = maskPixels(current, frame, bg);

    // Proposed pipeline: absdiff keeps both halves.
    ProcessingConfig proposed = baseConfig();
    proposed.proposed_pipeline = true;
    proposed.otsu_scale = 1.0;
    const int propPx = maskPixels(proposed, frame, bg);

    const int diskArea = static_cast<int>(CV_PI * 20 * 20);  // ~1256

    if (curPx <= 0) {
        std::cerr << "current: bright half should segment, got " << curPx << "\n";
        return 1;
    }
    // Current keeps ~half the disk; assert it clearly misses the dark half.
    if (curPx > diskArea * 3 / 4) {
        std::cerr << "current: signed subtract should miss the dark half (got "
                  << curPx << " of ~" << diskArea << ")\n";
        return 2;
    }
    // Proposed recovers the whole disk — well more than the current half.
    if (propPx < curPx * 3 / 2) {
        std::cerr << "proposed: absdiff should recover the dark half (proposed "
                  << propPx << " vs current " << curPx << ")\n";
        return 3;
    }
    if (propPx < diskArea / 2) {
        std::cerr << "proposed: mask should approach the full disk (~" << diskArea
                  << "), got " << propPx << "\n";
        return 4;
    }

    return 0;
}

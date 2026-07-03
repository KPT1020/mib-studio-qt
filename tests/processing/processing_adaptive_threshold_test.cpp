// Adaptive (Otsu) segmentation threshold — invariant + regression coverage for
// ProcessingService::applyProcessingThreshold (see benchmarks/mask-gen/REPORT.md).
//
// Contract under test:
//   1. Regression: with adaptive_threshold=false the mask equals a plain fixed
//      cv::threshold at bg_subtract_threshold (the refactor changed nothing).
//   2. Benefit: with adaptive_threshold=true, Otsu adapts the cut per frame and
//      rejects a bright background/noise floor that a low fixed threshold floods
//      on, while still segmenting the cell.
//   3. Floor safety: Otsu is floored at bg_subtract_threshold, so a frame whose
//      only content sits below that floor stays empty (no hallucinated
//      foreground) — the property that makes Otsu safe on empty frames.

#include "backend/processing/ProcessingService.h"

#include <opencv2/imgproc.hpp>

#include <iostream>

using backend::services::ProcessingConfig;
using backend::services::ProcessingService;
using Roi = ProcessingService::Roi;

namespace {

ProcessingConfig baseConfig() {
    ProcessingConfig c;
    c.gaussian_blur_size = 1;  // identity blur — keep synthetic values crisp
    c.morph_kernel_size = 1;   // identity morphology — mask == threshold result
    c.morph_iterations = 1;
    c.enable_border_check = false;
    c.enable_area_range_check = false;
    c.enable_deformability_range_check = false;
    c.enable_ring_ratio_check = false;
    c.enable_area_ratio_check = false;
    c.require_single_inner_contour = false;
    c.empty_frame_pixel_threshold = 1;
    return c;
}

// Bright disk (value `cell`) on a uniform floor (value `floorVal`).
cv::Mat makeFrame(int cell, int floorVal, int radius = 15) {
    cv::Mat img(120, 120, CV_8UC1, cv::Scalar(floorVal));
    cv::circle(img, {60, 60}, radius, cv::Scalar(cell), cv::FILLED);
    return img;
}

int maskPixels(const ProcessingConfig& c, const cv::Mat& frame) {
    auto pf = ProcessingService{}.computeProcessedFrame(frame, cv::Mat{}, c, Roi{0, 0, 0, 0}, 0, 0);
    return cv::countNonZero(pf.processedImage);
}

} // namespace

int main() {
    // ---- 1. Regression: fixed path unchanged -----------------------------
    {
        ProcessingConfig c = baseConfig();
        c.adaptive_threshold = false;
        c.bg_subtract_threshold = 100;
        const cv::Mat frame = makeFrame(/*cell=*/200, /*floor=*/0);

        cv::Mat ref;
        cv::threshold(frame, ref, 100, 255, cv::THRESH_BINARY);
        const int refPixels = cv::countNonZero(ref);
        const int got = maskPixels(c, frame);
        if (got != refPixels) {
            std::cerr << "regression: fixed-threshold mask changed (" << got
                      << " vs " << refPixels << ")\n";
            return 1;
        }
        if (got <= 0) {
            std::cerr << "regression: fixed threshold should detect the cell\n";
            return 2;
        }
    }

    // ---- 2. Benefit: Otsu rejects a bright floor a low fixed cut floods on -
    {
        const cv::Mat frame = makeFrame(/*cell=*/200, /*floor=*/30);

        ProcessingConfig fixedLow = baseConfig();
        fixedLow.adaptive_threshold = false;
        fixedLow.bg_subtract_threshold = 10;  // below the 30 floor -> floods
        const int fixedPixels = maskPixels(fixedLow, frame);

        ProcessingConfig adaptive = baseConfig();
        adaptive.adaptive_threshold = true;
        adaptive.bg_subtract_threshold = 10;  // same floor
        adaptive.otsu_scale = 1.0;
        const int adaptivePixels = maskPixels(adaptive, frame);

        const int cellArea = static_cast<int>(CV_PI * 15 * 15);  // ~707
        if (fixedPixels < 100 * 100) {
            std::cerr << "benefit: low fixed threshold should flood on the bright floor, got "
                      << fixedPixels << "\n";
            return 3;
        }
        if (adaptivePixels <= 0 || adaptivePixels > 3 * cellArea) {
            std::cerr << "benefit: adaptive mask should isolate the cell (~" << cellArea
                      << " px), got " << adaptivePixels << "\n";
            return 4;
        }
        if (adaptivePixels >= fixedPixels / 4) {
            std::cerr << "benefit: adaptive mask should be far tighter than the flooded fixed mask ("
                      << adaptivePixels << " vs " << fixedPixels << ")\n";
            return 5;
        }
    }

    // ---- 3. Floor safety: sub-floor content stays empty ------------------
    {
        ProcessingConfig c = baseConfig();
        c.adaptive_threshold = true;
        c.bg_subtract_threshold = 20;
        c.otsu_scale = 1.0;
        // Faint blob (value 12) entirely below the 20 floor, on black.
        const cv::Mat frame = makeFrame(/*cell=*/12, /*floor=*/0);
        const int px = maskPixels(c, frame);
        if (px != 0) {
            std::cerr << "floor safety: sub-floor content must not produce foreground, got "
                      << px << "\n";
            return 6;
        }
    }

    return 0;
}

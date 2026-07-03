// Decoupled size measurement — the safeguard that keeps `area` (and the
// deformability / E-modulus derived from the same contour) from drifting with
// the per-frame Otsu cut when adaptive detection is enabled.
//
// Contract under test (see benchmarks/mask-gen/REPORT.md):
//   Adaptive detection tightens the mask per frame, which — if size were read
//   from that mask — would move `area` with scene contrast. When
//   adaptive_threshold is on, ProcessingService measures area/areaRatio/
//   deformability from a *fixed-threshold* measurement mask instead, so:
//     1. Detection still tightens: the adaptive mask has fewer pixels than the
//        fixed mask (adaptive is doing its job on the segmentation).
//     2. Measurement is stable: the reported `area` equals the fixed-threshold
//        (adaptive-off) area, NOT the tighter adaptive-mask area — i.e. the
//        drift that would otherwise reach downstream analysis is removed.

#include "backend/processing/ProcessingService.h"

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <iostream>
#include <vector>

using backend::services::ProcessedFrame;
using backend::services::ProcessingConfig;
using backend::services::ProcessingService;
using Roi = ProcessingService::Roi;

namespace {

ProcessingConfig baseConfig() {
    ProcessingConfig c;
    c.gaussian_blur_size = 11;  // soften the disk edge into a gradient so the
                                // threshold level actually moves the boundary
    c.morph_kernel_size = 1;    // identity morphology — mask == threshold result
    c.morph_iterations = 1;
    c.enable_border_check = false;
    c.enable_area_range_check = false;
    c.enable_deformability_range_check = false;
    c.enable_ring_ratio_check = false;
    c.enable_area_ratio_check = false;
    c.require_single_inner_contour = false;  // solid disk -> outer-contour path
    c.empty_frame_pixel_threshold = 1;
    c.bg_subtract_threshold = 10;            // fixed floor / measurement level
    return c;
}

// Solid bright disk (value `peak`) on a 0 field (background-subtracted regime).
cv::Mat makeDisk(int peak, int radius = 40) {
    cv::Mat img(200, 200, CV_8UC1, cv::Scalar(0));
    cv::circle(img, {100, 100}, radius, cv::Scalar(peak), cv::FILLED);
    return img;
}

ProcessedFrame process(const ProcessingConfig& c, const cv::Mat& frame) {
    return ProcessingService{}.computeProcessedFrame(frame, cv::Mat{}, c, Roi{0, 0, 0, 0}, 0, 0);
}

// Convex-hull area of the largest contour in a binary mask — i.e. the area that
// WOULD be reported if size were read straight off this mask.
double largestHullArea(const cv::Mat& mask) {
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    double best = 0.0;
    for (const auto& c : contours) {
        std::vector<cv::Point> hull;
        cv::convexHull(c, hull);
        best = std::max(best, cv::contourArea(hull));
    }
    return best;
}

} // namespace

int main() {
    const cv::Mat frame = makeDisk(/*peak=*/120);

    // Fixed-threshold reference: this is the calibrated size basis.
    ProcessingConfig fixed = baseConfig();
    fixed.adaptive_threshold = false;
    const auto fixedFrame = process(fixed, frame);
    const double areaFixed = fixedFrame.validation.area;
    const int fixedPixels = cv::countNonZero(fixedFrame.processedImage);

    if (areaFixed <= 0.0) {
        std::cerr << "setup: fixed threshold should segment the disk, area=" << areaFixed << "\n";
        return 1;
    }

    // Adaptive detection with a tightening scale.
    ProcessingConfig adaptive = baseConfig();
    adaptive.adaptive_threshold = true;
    adaptive.otsu_scale = 1.3;  // push the Otsu cut up -> visibly tighter mask
    const auto adaptiveFrame = process(adaptive, frame);
    const double areaAdaptiveReported = adaptiveFrame.validation.area;
    const int adaptivePixels = cv::countNonZero(adaptiveFrame.processedImage);

    // ---- 1. Detection genuinely tightened -------------------------------
    if (!(adaptivePixels < fixedPixels)) {
        std::cerr << "detection: adaptive mask should be tighter than the fixed mask ("
                  << adaptivePixels << " vs " << fixedPixels << ")\n";
        return 2;
    }

    // The area that the (tighter) adaptive mask would yield on its own — the
    // drift the safeguard is meant to remove.
    const double areaAdaptiveMask = largestHullArea(adaptiveFrame.processedImage);
    if (!(areaAdaptiveMask < areaFixed * 0.95)) {
        std::cerr << "premise: adaptive mask area should be meaningfully smaller than the "
                     "fixed basis so there is real drift to prevent (" << areaAdaptiveMask
                  << " vs " << areaFixed << ")\n";
        return 3;
    }

    // ---- 2. Reported size stayed on the fixed basis, not the adaptive mask -
    const double relToFixed = std::abs(areaAdaptiveReported - areaFixed) / areaFixed;
    if (relToFixed > 0.02) {
        std::cerr << "safeguard: adaptive-mode area should match the fixed basis ("
                  << areaAdaptiveReported << " vs " << areaFixed << ", rel=" << relToFixed << ")\n";
        return 4;
    }

    // ...and is clearly NOT the drifted adaptive-mask area.
    const double relToDrift = std::abs(areaAdaptiveReported - areaAdaptiveMask) / areaFixed;
    if (relToDrift < 0.05) {
        std::cerr << "safeguard: adaptive-mode area collapsed onto the drifted adaptive-mask area ("
                  << areaAdaptiveReported << " vs " << areaAdaptiveMask << ")\n";
        return 5;
    }

    // ---- 3. Batch path is decoupled too --------------------------------
    // processBatch re-runs filterProcessedObjects on the detection mask to get
    // every object for tracking; it must forward the measurement mask so the
    // offline / re-analysis path (which feeds downstream analysis) stays on the
    // fixed size basis rather than the adaptive mask.
    ProcessingService svc;
    const auto batchFixed = svc.processBatch({frame}, fixed);
    const auto batchAdaptive = svc.processBatch({frame}, adaptive);
    if (batchFixed.empty() || batchAdaptive.empty()) {
        std::cerr << "batch: expected at least one processed frame\n";
        return 6;
    }
    const double batchAreaFixed = batchFixed.front().validation.area;
    const double batchAreaAdaptive = batchAdaptive.front().validation.area;
    if (batchAreaFixed <= 0.0) {
        std::cerr << "batch setup: fixed batch area should be positive, got " << batchAreaFixed << "\n";
        return 7;
    }
    const double batchRel = std::abs(batchAreaAdaptive - batchAreaFixed) / batchAreaFixed;
    if (batchRel > 0.02) {
        std::cerr << "batch safeguard: adaptive batch area should match the fixed basis ("
                  << batchAreaAdaptive << " vs " << batchAreaFixed << ", rel=" << batchRel << ")\n";
        return 8;
    }

    return 0;
}

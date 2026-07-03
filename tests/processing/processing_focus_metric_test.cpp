// Topology-free focus metrics (focusLaplacianVar / focusTenengrad) — see
// benchmarks/mask-gen/REPORT.md. Unlike the nested-contour ring ratio these are
// always defined (no closed ring required). Contract:
//   1. both metrics are populated (> 0) for a textured cell;
//   2. a sharp cell scores higher than a defocused (blurred) one — the metric
//      tracks focus;
//   3. the optional focus gate is off by default (no behaviour change) and,
//      when enabled, rejects the low-focus cell.

#include "backend/processing/ProcessingService.h"

#include <opencv2/imgproc.hpp>

#include <iostream>

using backend::services::FilterResult;
using backend::services::ProcessingConfig;
using backend::services::ProcessingService;
using Roi = ProcessingService::Roi;

namespace {

ProcessingConfig baseConfig() {
    ProcessingConfig c;
    c.gaussian_blur_size = 1;
    c.bg_subtract_threshold = 40;
    c.morph_kernel_size = 1;
    c.enable_border_check = false;
    c.enable_area_range_check = false;
    c.enable_deformability_range_check = false;
    c.enable_ring_ratio_check = false;
    c.enable_area_ratio_check = false;
    c.require_single_inner_contour = false;  // solid disk -> outer-contour object
    c.empty_frame_pixel_threshold = 1;
    return c;
}

// Textured disk so the interior has gradient energy, not just an edge ring.
cv::Mat makeDisk() {
    cv::Mat img(120, 120, CV_8UC1, cv::Scalar(0));
    cv::circle(img, {60, 60}, 26, cv::Scalar(220), cv::FILLED);
    cv::circle(img, {60, 60}, 14, cv::Scalar(90), cv::FILLED);   // inner intensity step
    cv::circle(img, {60, 60}, 6, cv::Scalar(255), cv::FILLED);
    return img;
}

FilterResult eval(const ProcessingConfig& c, const cv::Mat& frame) {
    return ProcessingService{}
        .computeProcessedFrame(frame, cv::Mat{}, c, Roi{0, 0, 0, 0}, 0, 0)
        .validation;
}

} // namespace

int main() {
    const cv::Mat sharp = makeDisk();
    cv::Mat blurred;
    cv::GaussianBlur(sharp, blurred, cv::Size(0, 0), 3.0);  // simulate defocus

    // ---- 1 & 2: metrics populated and track focus ------------------------
    const ProcessingConfig base = baseConfig();
    const auto s = eval(base, sharp);
    const auto b = eval(base, blurred);

    if (!s.isValid || !b.isValid) {
        std::cerr << "both cells should be detected/valid with the gate off\n";
        return 1;
    }
    if (s.focusLaplacianVar <= 0.0 || s.focusTenengrad <= 0.0) {
        std::cerr << "focus metrics should be populated for a textured cell\n";
        return 2;
    }
    if (!(s.focusLaplacianVar > b.focusLaplacianVar)) {
        std::cerr << "sharp cell should score higher focusLaplacianVar than blurred ("
                  << s.focusLaplacianVar << " vs " << b.focusLaplacianVar << ")\n";
        return 3;
    }
    if (!(s.focusTenengrad > b.focusTenengrad)) {
        std::cerr << "sharp cell should score higher focusTenengrad than blurred ("
                  << s.focusTenengrad << " vs " << b.focusTenengrad << ")\n";
        return 4;
    }

    // ---- 3: optional gate rejects the low-focus cell when enabled ---------
    ProcessingConfig gated = baseConfig();
    gated.enable_focus_check = true;
    gated.focus_laplacian_min = 0.5 * (s.focusLaplacianVar + b.focusLaplacianVar);
    const auto sg = eval(gated, sharp);
    const auto bg = eval(gated, blurred);
    if (!sg.isValid) {
        std::cerr << "focus gate should keep the in-focus cell valid\n";
        return 5;
    }
    if (bg.isValid) {
        std::cerr << "focus gate should reject the defocused cell\n";
        return 6;
    }

    return 0;
}

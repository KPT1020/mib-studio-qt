// recording_isframeempty_roi_test
//
// Verifies that the ROI-only isFrameEmpty overload (shared_ptr<const cv::Mat>
// background, no full-frame copy) produces identical classifications to the
// full-frame overload on a range of synthetic frames: uniform background, bright
// blob, partial illumination, and with/without a background image.
//
// This is a correctness invariant: the hot-path overload shipped in PR2 must
// classify exactly the same as the original for every frame.

#include "backend/processing/ProcessingService.h"
#include "backend/playback/FrameStore.h"

#include "support/assert.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <cstdio>
#include <cstdint>
#include <memory>
#include <vector>

using PS  = backend::services::ProcessingService;
using Roi = PS::Roi;
using PC  = backend::services::ProcessingConfig;
using backend::playback::Frame;

namespace {

// Build a Frame from a cv::Mat (must be CV_8UC1).
Frame matToFrame(const cv::Mat& m)
{
    Frame f;
    f.width  = static_cast<uint64_t>(m.cols);
    f.height = static_cast<uint64_t>(m.rows);
    f.linePitch = static_cast<size_t>(m.cols);
    f.timestamp = 0;
    f.data.assign(m.datastart, m.dataend);
    return f;
}

struct Case {
    const char* label;
    cv::Mat     frame;
    cv::Mat     background; // may be empty
    Roi         roi;
};

PC defaultConfig()
{
    PC c;
    c.gaussian_blur_size          = 3;
    c.bg_subtract_threshold       = 8;
    c.empty_frame_pixel_threshold = 100;
    return c;
}

} // namespace

int main()
{
    constexpr int W = 256, H = 256;

    cv::Mat dark(H, W, CV_8UC1, cv::Scalar(10));
    cv::Mat bright(H, W, CV_8UC1, cv::Scalar(200));

    // Frame with a blob in the upper-left quadrant
    cv::Mat blob(H, W, CV_8UC1, cv::Scalar(10));
    cv::circle(blob, cv::Point(64, 64), 30, cv::Scalar(200), -1);

    // Frame with blob only in the lower-right quadrant
    cv::Mat blobLR(H, W, CV_8UC1, cv::Scalar(10));
    cv::circle(blobLR, cv::Point(192, 192), 30, cv::Scalar(200), -1);

    // Background image (dark)
    cv::Mat bg(H, W, CV_8UC1, cv::Scalar(10));

    PC cfg = defaultConfig();

    // Cases: (label, frame, background or empty, roi)
    std::vector<Case> cases = {
        {"uniform-dark-no-bg-fullroi",  dark,   cv::Mat{}, {0, 0, W, H}},
        {"uniform-dark-with-bg-fullroi", dark,  bg,        {0, 0, W, H}},
        {"uniform-bright-no-bg-fullroi", bright, cv::Mat{}, {0, 0, W, H}},
        {"blob-UL-roi-UL",  blob, bg, {0, 0, 128, 128}},   // blob inside roi → non-empty
        {"blob-UL-roi-LR",  blob, bg, {128, 128, 128, 128}}, // blob outside roi → empty
        {"blob-LR-roi-UL",  blobLR, bg, {0, 0, 128, 128}},  // blob outside roi → empty
        {"blob-LR-roi-LR",  blobLR, bg, {128, 128, 128, 128}}, // blob inside roi → non-empty
        {"dark-no-bg-small-roi", dark, cv::Mat{}, {50, 50, 64, 64}},
        {"blob-UL-no-bg-fullroi", blob, cv::Mat{}, {0, 0, W, H}},
    };

    std::printf("=== recording_isframeempty_roi_test ===\n");

    for (const auto& c : cases) {
        Frame f = matToFrame(c.frame);

        // Full-frame overload (reference)
        const bool legacyResult = PS::isFrameEmpty(f, cfg, c.roi, c.background);

        // ROI-only shared_ptr overload (hot-path)
        std::shared_ptr<const cv::Mat> bgPtr;
        if (!c.background.empty()) {
            bgPtr = std::make_shared<const cv::Mat>(c.background);
        }
        const bool newResult = PS::isFrameEmpty(f, cfg, c.roi, bgPtr);

        MIB_EXPECT(legacyResult == newResult,
                   std::string(c.label) + ": ROI overload disagrees with full-frame overload");

        std::printf("  %-40s legacy=%d new=%d %s\n",
                    c.label, (int)legacyResult, (int)newResult,
                    legacyResult == newResult ? "OK" : "FAIL");
    }

    if (mib::test::exitCode() == 0) {
        std::printf("All cases: ROI-only isFrameEmpty agrees with full-frame overload\n");
    }
    return mib::test::exitCode();
}

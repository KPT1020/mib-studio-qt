// ProcessingService algorithm throughput benchmarks.
//
// ProcessingService::getAlgoAvgUs1s() is the realtime "how long did the
// algorithm take per frame?" metric the UI displays. This test pins
// down the ground-truth value by running computeProcessedFrame (the
// pure, side-effect-free variant of the realtime pipeline) on
// synthetic frames and reporting latency percentiles.
//
// Matrix:
//   - Frame sizes: 512x512, 1024x1024, 2048x2048
//   - Background subtraction: off / on
//   - ROI: full frame / 50%x50% centred
//
// That's 12 cells. Each cell runs a fixed number of iterations
// (overridable via MIB_PROCESSING_ITERS) and reports the full latency
// distribution plus implied FPS (1e6 / mean_us).
//
// What to look for in MLflow trends:
//   - Mean algo-us for 2048x2048 with background on = hot path in a
//     typical experiment. Regressions here directly hit captured FPS.
//   - ROI vs full-frame delta = lower-bound benefit of shrinking the
//     ROI; if that gap closes, the ROI early-exit in filterProcessedImage
//     probably regressed.

#include "backend/services/ProcessingService.h"
#include "perf_common.h"

#include <chrono>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <spdlog/spdlog.h>

using mib::perf::LatencyStats;
using mib::perf::summarise;
using mib::perf::logStats;
using mib::perf::envSizeOr;
using perf_clock = std::chrono::steady_clock;

namespace {

struct FrameSize {
    const char* label;
    int width;
    int height;
};

constexpr FrameSize kSizes[] = {
    {"512x512",   512,  512},
    {"1024x1024", 1024, 1024},
    {"2048x2048", 2048, 2048},
};

cv::Mat makeSyntheticFrame(int w, int h, uint32_t seed) {
    // Simulate a microscopy-ish frame: mid-gray background + a handful of
    // brighter ellipse blobs. Gives contours to process without being
    // pathological (e.g., a perfect noise field produces far too many
    // small contours and would dominate the bench with post-filter work).
    cv::Mat img(h, w, CV_8UC1, cv::Scalar(50));
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> xDist(20, w - 20);
    std::uniform_int_distribution<int> yDist(20, h - 20);
    std::uniform_int_distribution<int> rxDist(5, 20);
    std::uniform_int_distribution<int> ryDist(5, 20);
    const int numBlobs = std::max(3, (w * h) / (128 * 128)); // scales with area
    for (int i = 0; i < numBlobs; ++i) {
        cv::ellipse(img,
                    cv::Point(xDist(rng), yDist(rng)),
                    cv::Size(rxDist(rng), ryDist(rng)),
                    0, 0, 360,
                    cv::Scalar(200 + (i % 55)),
                    -1);
    }
    // Add light noise so the Gaussian blur + threshold pipeline does
    // non-trivial work rather than hitting degenerate paths.
    cv::Mat noise(h, w, CV_8UC1);
    cv::randn(noise, 0, 5);
    cv::add(img, noise, img);
    return img;
}

cv::Mat makeSyntheticBackground(int w, int h) {
    cv::Mat bg(h, w, CV_8UC1, cv::Scalar(50));
    cv::Mat noise(h, w, CV_8UC1);
    cv::randn(noise, 0, 2);
    cv::add(bg, noise, bg);
    return bg;
}

struct Cell {
    std::string key;
    std::string label;
    FrameSize fs;
    bool withBackground;
    bool fullRoi;
};

LatencyStats benchCell(const Cell& c, std::size_t iterations) {
    backend::services::ProcessingService svc;
    backend::services::ProcessingConfig cfg; // defaults from header

    const cv::Mat background = c.withBackground
        ? makeSyntheticBackground(c.fs.width, c.fs.height)
        : cv::Mat{};

    backend::services::ProcessingService::Roi roi;
    if (c.fullRoi) {
        roi = {0, 0, c.fs.width, c.fs.height};
    } else {
        roi = {c.fs.width / 4, c.fs.height / 4,
               c.fs.width / 2, c.fs.height / 2};
    }

    // Pre-generate a small pool of frames so the per-iteration cost is
    // dominated by computeProcessedFrame, not by frame synthesis.
    constexpr std::size_t kPoolSize = 8;
    std::vector<cv::Mat> pool;
    pool.reserve(kPoolSize);
    for (std::size_t i = 0; i < kPoolSize; ++i) {
        pool.push_back(makeSyntheticFrame(c.fs.width, c.fs.height,
                                          static_cast<uint32_t>(0xC0FFEEull + i)));
    }

    // Warm-up — allocator + branch predictor settle.
    for (std::size_t i = 0; i < 3; ++i) {
        (void)svc.computeProcessedFrame(pool[i % pool.size()], background, cfg, roi,
                                        static_cast<uint64_t>(i),
                                        static_cast<uint64_t>(i) * 1'000'000ULL);
    }

    std::vector<double> samples;
    samples.reserve(iterations);
    for (std::size_t i = 0; i < iterations; ++i) {
        const auto& src = pool[i % pool.size()];
        const auto t0 = perf_clock::now();
        auto pf = svc.computeProcessedFrame(src, background, cfg, roi,
                                            static_cast<uint64_t>(i),
                                            static_cast<uint64_t>(i) * 1'000'000ULL);
        const auto t1 = perf_clock::now();
        // Touch the result so the optimiser can't remove it.
        if (pf.processedImage.empty()) break;
        samples.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }
    return summarise(std::move(samples));
}

} // namespace

int main() {
    try {
        spdlog::set_level(spdlog::level::info);

        const std::size_t iters = envSizeOr("MIB_PROCESSING_ITERS", 200);
        SPDLOG_INFO("ProcessingService perf | iters_per_cell={}", iters);

        std::vector<Cell> cells;
        cells.reserve(12);
        for (const auto& fs : kSizes) {
            for (bool withBg : {false, true}) {
                for (bool fullRoi : {true, false}) {
                    const std::string bgStr   = withBg  ? "bg"    : "nobg";
                    const std::string roiStr  = fullRoi ? "full"  : "roi50";
                    Cell c;
                    c.fs = fs;
                    c.withBackground = withBg;
                    c.fullRoi = fullRoi;
                    c.key   = std::string("compute_") + fs.label + "_" + bgStr + "_" + roiStr;
                    c.label = std::string("computeProcessedFrame ") + fs.label
                              + " [" + bgStr + "/" + roiStr + "]";
                    cells.push_back(std::move(c));
                }
            }
        }

        mib::perf::JsonReport report;
        for (const auto& c : cells) {
            const auto s = benchCell(c, iters);
            logStats(c.label, s);
            report.addStats(c.key, s);
            const double impliedFps = (s.meanUs > 0.0) ? (1'000'000.0 / s.meanUs) : 0.0;
            report.addNumber(c.key + "_implied_fps", impliedFps);
        }

        // No hard soft-ceilings here: algo time varies hugely between
        // Debug/Release and between runner generations. MLflow trend
        // watchers are the right place to catch regressions.

        const std::string jsonPath = mib::perf::resolveJsonOutPath(
            "MIB_PROCESSING_PERF_JSON", "processing_perf_results.json");
        if (!report.writeTo(jsonPath)) {
            SPDLOG_WARN("Failed to open {} for JSON report", jsonPath);
        } else {
            SPDLOG_INFO("Wrote JSON report: {}", jsonPath);
        }
        return 0;
    } catch (const std::exception& ex) {
        SPDLOG_ERROR("processing_perf_test exception: {}", ex.what());
        return 2;
    }
}

// Prototype A/B: current vs proposed segmentation front-end, in the real C++
// code path (computeProcessedFrame), scored on the GT dataset.
//
//   current  = ProcessingService default (signed cv::subtract, fixed threshold)
//   proposed = config.proposed_pipeline (cv::absdiff + Otsu + ellipse close)
//
// Reports mean IoU / detection@0.5 / area error vs the SAM2 GT masks, and median
// per-frame latency for each pipeline. Needs the benchmark cache: run
// benchmarks/mask-gen/download_gt.py then the pairs.csv generator. The manifest
// path comes from argv[1] or $MIB_PROPOSED_BENCH_PAIRS; ABSENT DATA -> skip (0),
// so CI (which has no dataset) stays green. When data is present it also asserts
// proposed IoU > current IoU, guarding the port against regressions.

#include "backend/processing/ProcessingService.h"

#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using backend::services::ProcessingConfig;
using backend::services::ProcessingService;
using Roi = ProcessingService::Roi;
using clock_t_ = std::chrono::steady_clock;

namespace {

// Channel band (dataset interior plateau) — matches pipelines.ROI_Y0/Y1.
constexpr int kRoiY0 = 12, kRoiY1 = 70;

struct Pair {
    std::string image, gt;
    int bx0, by0, bx1, by1;
};

std::vector<Pair> loadPairs(const std::string& path) {
    std::vector<Pair> out;
    std::ifstream f(path);
    if (!f) return out;
    std::string line;
    std::getline(f, line);  // header
    while (std::getline(f, line)) {
        std::stringstream ss(line);
        std::string img, gt, a, b, c, d;
        if (!std::getline(ss, img, ',') || !std::getline(ss, gt, ',') ||
            !std::getline(ss, a, ',') || !std::getline(ss, b, ',') ||
            !std::getline(ss, c, ',') || !std::getline(ss, d, ',')) continue;
        out.push_back({img, gt, std::stoi(a), std::stoi(b), std::stoi(c), std::stoi(d)});
    }
    return out;
}

// Per-row median background (matches pipelines.estimate_background).
cv::Mat perRowMedianBg(const cv::Mat& gray) {
    cv::Mat bg(gray.size(), CV_8UC1);
    std::vector<uchar> row(gray.cols);
    for (int y = 0; y < gray.rows; ++y) {
        const uchar* p = gray.ptr<uchar>(y);
        std::copy(p, p + gray.cols, row.begin());
        std::nth_element(row.begin(), row.begin() + row.size() / 2, row.end());
        bg.row(y).setTo(row[row.size() / 2]);
    }
    return bg;
}

// findContours + fill (>=10 px), matching pipelines.fill_contours.
cv::Mat fillMask(const cv::Mat& m) {
    std::vector<std::vector<cv::Point>> cs;
    cv::findContours(m, cs, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    cv::Mat out = cv::Mat::zeros(m.size(), CV_8UC1);
    for (const auto& c : cs) {
        if (cv::contourArea(c) >= 10.0) {
            cv::drawContours(out, std::vector<std::vector<cv::Point>>{c}, -1, cv::Scalar(255), cv::FILLED);
        }
    }
    return out;
}

cv::Rect crop(const Pair& p, const cv::Size& sz, int pad = 6) {
    int x0 = std::max(0, p.bx0 - pad), y0 = std::max(0, p.by0 - pad);
    int x1 = std::min(sz.width, p.bx1 + pad), y1 = std::min(sz.height, p.by1 + pad);
    return cv::Rect(x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0));
}

struct Score { double iou = 0, area_err = 0; int detected = 0; };

Score score(const cv::Mat& maskFilled, const cv::Mat& gt, const Pair& p) {
    const cv::Rect r = crop(p, gt.size());
    Score s;
    if (r.area() <= 0) return s;
    cv::Mat pc = maskFilled(r) > 0, gc = gt(r) > 0;
    const double inter = cv::countNonZero(pc & gc);
    const double uni = cv::countNonZero(pc | gc);
    const double pa = cv::countNonZero(pc), ga = cv::countNonZero(gc);
    s.iou = uni > 0 ? inter / uni : 1.0;
    s.detected = s.iou > 0.5 ? 1 : 0;
    s.area_err = ga > 0 ? std::abs(pa - ga) / ga : 0.0;
    return s;
}

double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

ProcessingConfig currentCfg() {
    ProcessingConfig c;
    c.gaussian_blur_size = 3;
    c.morph_kernel_size = 3;
    c.bg_subtract_threshold = 8;
    c.adaptive_threshold = false;
    c.require_single_inner_contour = false;
    c.enable_border_check = false;
    c.enable_area_range_check = false;
    c.enable_ring_ratio_check = false;
    c.empty_frame_pixel_threshold = 1;
    return c;
}

ProcessingConfig proposedCfg() {
    ProcessingConfig c = currentCfg();
    c.proposed_pipeline = true;
    c.otsu_scale = 1.1;
    c.morph_kernel_size = 5;  // proposed LEAN closes with a 5x5 ellipse
    return c;
}

struct Result { double meanIoU = 0, det = 0, areaErrMed = 0, latMsMed = 0; };

Result run(const std::vector<Pair>& pairs, const std::vector<cv::Mat>& grays,
           const std::vector<cv::Mat>& bgs, const std::vector<cv::Mat>& gts,
           const ProcessingConfig& cfg, int reps) {
    ProcessingService svc;
    const Roi roi{0, kRoiY0, 0, kRoiY1 - kRoiY0};  // full width, channel band
    std::vector<double> ious, aerrs, lats;
    for (size_t i = 0; i < pairs.size(); ++i) {
        Roi r = roi; r.w = grays[i].cols;
        // Accuracy (one pass).
        auto pf = svc.computeProcessedFrame(grays[i], bgs[i], cfg, r, 0, 0);
        const Score s = score(fillMask(pf.processedImage), gts[i], pairs[i]);
        ious.push_back(s.iou); aerrs.push_back(s.area_err);
        // Latency (segmentation only, reps).
        for (int k = 0; k < reps; ++k) {
            const auto t0 = clock_t_::now();
            volatile auto pf2 = svc.computeProcessedFrame(grays[i], bgs[i], cfg, r, 0, 0);
            const auto t1 = clock_t_::now();
            (void)pf2;
            lats.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        }
    }
    Result out;
    out.meanIoU = 0; for (double v : ious) out.meanIoU += v; out.meanIoU /= ious.size();
    int det = 0; for (size_t i = 0; i < ious.size(); ++i) det += ious[i] > 0.5 ? 1 : 0;
    out.det = 100.0 * det / ious.size();
    out.areaErrMed = median(aerrs) * 100.0;
    out.latMsMed = median(lats);
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    std::string manifest;
    if (argc > 1) manifest = argv[1];
    else if (const char* e = std::getenv("MIB_PROPOSED_BENCH_PAIRS")) manifest = e;
    else manifest = "benchmarks/mask-gen/cache/pairs.csv";

    const std::vector<Pair> pairs = loadPairs(manifest);
    if (pairs.empty()) {
        std::cout << "processing.proposed_pipeline_bench: SKIP (no dataset at "
                  << manifest << ")\n";
        return 0;  // no data in CI -> not a failure
    }

    std::vector<cv::Mat> grays, bgs, gts;
    for (const auto& p : pairs) {
        cv::Mat g = cv::imread(p.image, cv::IMREAD_GRAYSCALE);
        cv::Mat t = cv::imread(p.gt, cv::IMREAD_GRAYSCALE);
        if (g.empty() || t.empty()) continue;
        grays.push_back(g); gts.push_back(t); bgs.push_back(perRowMedianBg(g));
    }
    std::cout << "loaded " << grays.size() << " frames (" << grays[0].cols << "x"
              << grays[0].rows << ")\n";

    const int reps = 20;
    const Result cur = run(pairs, grays, bgs, gts, currentCfg(), reps);
    const Result prop = run(pairs, grays, bgs, gts, proposedCfg(), reps);

    auto line = [](const char* n, const Result& r) {
        std::cout << "  " << n << "  IoU " << r.meanIoU << "  det@0.5 " << r.det
                  << "%  areaErr(med) " << r.areaErrMed << "%  lat(med) "
                  << r.latMsMed << " ms\n";
    };
    std::cout.setf(std::ios::fixed); std::cout.precision(3);
    std::cout << "\n== current vs proposed (C++ computeProcessedFrame) ==\n";
    line("current ", cur);
    line("proposed", prop);
    std::cout << "  speed ratio (proposed/current lat): "
              << (cur.latMsMed > 0 ? prop.latMsMed / cur.latMsMed : 0.0) << "x\n";

    if (!(prop.meanIoU > cur.meanIoU)) {
        std::cerr << "port regression: proposed IoU (" << prop.meanIoU
                  << ") should exceed current (" << cur.meanIoU << ")\n";
        return 1;
    }
    return 0;
}

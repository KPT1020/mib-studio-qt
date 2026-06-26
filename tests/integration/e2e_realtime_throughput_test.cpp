// e2e_realtime_throughput_test
//
// Performance regression guard for the realtime processing path (full-frame and
// ROI). Pushes frames as fast as possible into a FrameStore and runs the inline
// realtime loop, then measures:
//   * processing throughput (frames the loop actually completed per second)
//   * steady-state overlay lag (latestAvailableIndex - processed snapshot index)
// With drop-frames the default, the loop must keep up: lag stays bounded and it
// makes real progress. Throughput numbers are reported for visibility. Gates are
// machine-independent (progress + bounded lag), not absolute timings.

#include "backend/processing/ProcessingService.h"
#include "backend/playback/FrameStore.h"

#include "support/assert.h"
#include "support/stats.h"
#include "support/watchdog.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

using backend::services::ProcessingService;
using backend::services::ProcessingConfig;
using backend::playback::FrameStore;
using Clock = std::chrono::steady_clock;

namespace {

constexpr int kW = 256;
constexpr int kH = 256;
constexpr uint64_t kMono8 = 0x01080001ULL;

ProcessingConfig lenient()
{
    ProcessingConfig c;
    c.gaussian_blur_size = 3;
    c.bg_subtract_threshold = 8;
    c.morph_kernel_size = 3;
    c.morph_iterations = 1;
    c.enable_border_check = false;
    c.enable_area_range_check = false;
    c.enable_deformability_range_check = false;
    c.enable_area_ratio_check = false;
    c.enable_ring_ratio_check = false;
    c.require_single_inner_contour = false;
    c.empty_frame_pixel_threshold = 1;
    c.auto_background_enabled = false;
    c.multi_image_enabled = false;
    return c;
}

cv::Mat blob(uint64_t i)
{
    cv::Mat m(kH, kW, CV_8UC1, cv::Scalar(0));
    cv::circle(m, cv::Point(64 + static_cast<int>(i % 64), 128), 40, cv::Scalar(220), -1);
    return m;
}

struct Result {
    double processedFps{0.0};
    double lateLag{0.0};
    uint64_t processedAdvance{0};
    uint64_t pushed{0};
};

Result run(const char* label, bool useRoi, int durationMs)
{
    auto store = std::make_shared<FrameStore>(5000);
    ProcessingService proc;
    proc.setProcessingConfig(lenient());
    proc.setRealtimeProcessingMode(ProcessingService::RealtimeProcessingMode::Inline);
    if (useRoi) {
        proc.setRealtimeRoi(ProcessingService::Roi{32, 32, 160, 160});
    }
    // drop-frames left at its default (ON) — the live-view path.
    proc.startRealtime(store);
    proc.setRealtimeEnabled(true);

    constexpr uint64_t kPre = 64;
    std::vector<cv::Mat> frames;
    for (uint64_t k = 0; k < kPre; ++k) frames.push_back(blob(k));

    std::atomic<bool> producing{true};
    std::atomic<uint64_t> pushed{0};
    std::thread producer([&] {
        uint64_t i = 0;
        while (producing.load(std::memory_order_relaxed)) {
            const cv::Mat& f = frames[i % kPre];
            store->pushFrame(f.data, static_cast<size_t>(f.total()), kW, kH, kW, kMono8,
                             (i + 1) * 1000ULL);
            ++i;
            pushed.fetch_add(1, std::memory_order_relaxed);
            if ((i & 0x1F) == 0) std::this_thread::yield();
        }
    });

    ProcessingService::RealtimeSnapshot snap;
    uint64_t firstProcessed = 0;
    bool haveFirst = false;
    std::vector<double> lags;
    const auto start = Clock::now();
    while (std::chrono::duration<double, std::milli>(Clock::now() - start).count() < durationMs) {
        const uint64_t latest = store->latestAvailableIndex();
        if (proc.getLatestSnapshot(snap)) {
            if (!haveFirst) { firstProcessed = snap.index; haveFirst = true; }
            const uint64_t lag = latest > snap.index ? latest - snap.index : 0;
            lags.push_back(static_cast<double>(lag));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    const uint64_t lastProcessed = snap.index;

    producing.store(false);
    producer.join();
    proc.setRealtimeEnabled(false);
    proc.stopRealtime();

    Result r;
    r.pushed = pushed.load();
    r.processedAdvance = lastProcessed > firstProcessed ? lastProcessed - firstProcessed : 0;
    r.processedFps = r.processedAdvance / (durationMs / 1000.0);
    // steady-state lag = late-window average
    {
        std::vector<double> late;
        const size_t tail = lags.size() / 3;
        for (size_t i = lags.size() - std::min(tail, lags.size()); i < lags.size(); ++i)
            late.push_back(lags[i]);
        r.lateLag = mib::test::summarize(late).mean;
    }
    std::printf("%s: pushed=%llu processed_advance=%llu processed_fps=%.0f late_lag=%.1f\n",
                label, static_cast<unsigned long long>(r.pushed),
                static_cast<unsigned long long>(r.processedAdvance), r.processedFps, r.lateLag);
    return r;
}

} // namespace

int main()
{
    mib::test::Watchdog wd(40);
    wd.mark("full-frame");
    const Result full = run("realtime full-frame", /*useRoi=*/false, 2000);
    wd.mark("roi");
    const Result roi = run("realtime ROI       ", /*useRoi=*/true, 2000);

    // Machine-independent gates: the loop made real progress, and with
    // drop-frames the default the steady-state lag stayed bounded (did not run
    // away toward the ring capacity). Absolute fps is reported, not gated.
    MIB_EXPECT(full.processedAdvance > 100, "full-frame realtime made progress");
    MIB_EXPECT(roi.processedAdvance > 100, "ROI realtime made progress");
    MIB_EXPECT(full.lateLag < 2000.0, "full-frame steady-state overlay lag bounded");
    MIB_EXPECT(roi.lateLag < 2000.0, "ROI steady-state overlay lag bounded");

    if (mib::test::exitCode() == 0) {
        std::printf("realtime throughput OK (full-frame + ROI keep up; lag bounded)\n");
    }
    return mib::test::exitCode();
}

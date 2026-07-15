// realtime_background_cache_test
//
// Regression guard for issue #259 §2: the realtime inline loop caches the
// *blurred* background and re-blurs it only when the background actually changes
// (Set-Background / auto-capture), instead of Gaussian-blurring it on every
// frame. The optimization is only correct if the cache is invalidated when the
// background changes — a stale blurred copy would keep subtracting the OLD
// background and silently corrupt every subsequent detection.
//
// This test drives the real inline loop (drop-frames OFF, so every frame is
// processed in order) and asserts the observable invariant:
//   * Phase A — background == frame: background subtraction cancels the frame,
//     so it is detected as empty and produces no processed frames.
//   * Phase B — after Set-Background to a blank image: the same frame now differs
//     from the background and IS processed. This only holds if the cached blur
//     was recomputed for the new background.
// If the blurred-background cache failed to invalidate, Phase B would keep
// subtracting the Phase-A background and stay empty — this test would fail.

#include "backend/processing/ProcessingService.h"
#include "backend/playback/FrameStore.h"

#include "support/assert.h"
#include "support/watchdog.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

using backend::services::ProcessingService;
using backend::services::ProcessingConfig;
using backend::playback::FrameStore;
using backend::playback::Frame;
using Clock = std::chrono::steady_clock;

namespace {

constexpr int kW = 96;
constexpr int kH = 96;
constexpr uint64_t kMono8 = 0x01080001ULL;

ProcessingConfig detectAny()
{
    // All validation gates off: any non-empty object is accepted, so the only
    // thing that decides whether a frame is "processed" is the empty-frame check,
    // which is exactly what background subtraction feeds.
    ProcessingConfig c;
    c.gaussian_blur_size = 3;
    c.bg_subtract_threshold = 25;
    c.morph_kernel_size = 3;
    c.morph_iterations = 1;
    c.enable_border_check = false;
    c.enable_area_range_check = false;
    c.enable_deformability_range_check = false;
    c.enable_area_ratio_check = false;
    c.enable_ring_ratio_check = false;
    c.require_single_inner_contour = false;
    c.empty_frame_pixel_threshold = 10;
    c.auto_background_enabled = false; // no auto-capture / frame-to-frame diff
    c.multi_image_enabled = false;
    return c;
}

cv::Mat discImage()
{
    cv::Mat m(kH, kW, CV_8UC1, cv::Scalar(0));
    cv::circle(m, cv::Point(kW / 2, kH / 2), 20, cv::Scalar(220), -1);
    return m;
}

cv::Mat sentinelImage()
{
    // Uniformly bright: guaranteed non-empty against any background above, so its
    // published snapshot marks that every frame before it has been processed.
    return cv::Mat(kH, kW, CV_8UC1, cv::Scalar(255));
}

void push(FrameStore& store, const cv::Mat& img, uint64_t idx)
{
    store.pushFrame(img.data, static_cast<size_t>(img.total()), kW, kH, kW, kMono8,
                    (idx + 1) * 1000ULL);
}

// Drive one phase: push `count` copies of `frame` followed by a sentinel, then
// block (bounded by the watchdog) until the loop has processed through the
// sentinel. Returns the number of frames in [firstIdx, sentinelIdx) that the
// loop actually processed (i.e. treated as non-empty).
size_t runPhase(FrameStore& store, ProcessingService& proc, const cv::Mat& frame,
                int count, uint64_t& nextIdx)
{
    proc.clearMonitoringFrames();
    const uint64_t firstIdx = nextIdx;
    for (int i = 0; i < count; ++i) push(store, frame, nextIdx++);
    const uint64_t sentinelIdx = nextIdx;
    push(store, sentinelImage(), nextIdx++);

    // Wait until the sentinel (always non-empty) has been processed. drop-frames
    // is OFF and everything was pushed already, so in-order processing guarantees
    // all frames < sentinelIdx are done once the sentinel's snapshot appears.
    ProcessingService::RealtimeSnapshot snap;
    const auto start = Clock::now();
    while (std::chrono::duration<double>(Clock::now() - start).count() < 10.0) {
        if (proc.getLatestSnapshot(snap) && snap.index >= sentinelIdx) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    size_t processed = 0;
    auto tally = [&](const std::vector<backend::services::ProcessedFrame>& frames) {
        for (const auto& f : frames) {
            if (f.index >= firstIdx && f.index < sentinelIdx) ++processed;
        }
    };
    tally(proc.getMonitoringValidFrames());
    tally(proc.getMonitoringInvalidFrames());
    return processed;
}

} // namespace

int main()
{
    mib::test::Watchdog wd(40);

    auto store = std::make_shared<FrameStore>(4096);
    ProcessingService proc;
    proc.setProcessingConfig(detectAny());
    proc.setRealtimeProcessingMode(ProcessingService::RealtimeProcessingMode::Inline);
    proc.setRealtimeDropFrames(false); // process every frame, in order
    proc.setMonitoringActive(true);

    const cv::Mat disc = discImage();

    proc.startRealtime(store);
    proc.setRealtimeEnabled(true);

    uint64_t nextIdx = 0;

    // Phase A: background identical to the frame -> subtraction cancels -> empty.
    wd.mark("phase-A (frame == background)");
    proc.setRealtimeBackgroundGray(disc);
    const size_t processedA = runPhase(*store, proc, disc, 20, nextIdx);

    // Phase B: change background to blank -> the same frame now differs -> the
    // loop must re-blur the new background and process the frames.
    wd.mark("phase-B (background changed to blank)");
    proc.setRealtimeBackgroundGray(cv::Mat(kH, kW, CV_8UC1, cv::Scalar(0)));
    const size_t processedB = runPhase(*store, proc, disc, 20, nextIdx);

    proc.setRealtimeEnabled(false);
    proc.stopRealtime();

    std::printf("processed: phaseA(frame==bg)=%zu  phaseB(bg=blank)=%zu\n",
                processedA, processedB);

    // Frames equal to the background are subtracted to ~zero and suppressed.
    MIB_EXPECT(processedA == 0,
               "frames identical to the background produce no processed frames "
               "(background subtraction is active)");
    // After Set-Background the cached blur must be recomputed; otherwise the
    // frames would still be subtracted against the old background and stay empty.
    MIB_EXPECT(processedB >= 18,
               "after Set-Background, frames are processed against the NEW blurred "
               "background (blurred-background cache was invalidated)");
    MIB_EXPECT(processedB > processedA,
               "changing the background changes the detection outcome");

    if (mib::test::exitCode() == 0) {
        std::printf("realtime background-cache invalidation OK\n");
    }
    return mib::test::exitCode();
}

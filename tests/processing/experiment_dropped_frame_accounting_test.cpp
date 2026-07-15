// experiment_dropped_frame_accounting_test
//
// Guards issue #259 §7: during an experiment the realtime loop must not drop
// recordable frames *silently*. When capture outruns processing and the
// FrameStore ring window advances past the loop's read pointer, the skipped
// frames are counted in `experimentDroppedFrames_` and surfaced
// (`getExperimentDroppedFrameCount`, end-of-experiment WARN) so a run's data
// gaps are reported instead of vanishing.
//
// Deterministic setup: a deliberately tiny ring plus a large burst of frames
// forces the window to overtake the reader while an experiment is active, so the
// counter must become positive. The control case (no experiment) must leave it
// at zero — the counter is gated on `experimentActive_`.

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

using backend::services::ProcessingService;
using backend::services::ProcessingConfig;
using backend::playback::FrameStore;
using Clock = std::chrono::steady_clock;

namespace {

constexpr int kW = 96;
constexpr int kH = 96;
constexpr uint64_t kMono8 = 0x01080001ULL;

ProcessingConfig detectAny()
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
    c.empty_frame_pixel_threshold = 5;
    c.auto_background_enabled = false;
    c.multi_image_enabled = false;
    return c;
}

cv::Mat blob()
{
    cv::Mat m(kH, kW, CV_8UC1, cv::Scalar(0));
    cv::circle(m, cv::Point(kW / 2, kH / 2), 22, cv::Scalar(220), -1);
    return m;
}

// Flood a small ring with many non-empty frames so the window overtakes the
// realtime reader. Returns the dropped-frame count observed within the deadline.
uint64_t floodAndObserve(FrameStore& store, ProcessingService& proc, int frames,
                         bool expectDrops, double maxSeconds)
{
    const cv::Mat img = blob();
    for (int i = 0; i < frames; ++i) {
        store.pushFrame(img.data, static_cast<size_t>(img.total()), kW, kH, kW, kMono8,
                        static_cast<uint64_t>(i + 1) * 1000ULL);
    }
    const auto start = Clock::now();
    while (std::chrono::duration<double>(Clock::now() - start).count() < maxSeconds) {
        const uint64_t dropped = proc.getExperimentDroppedFrameCount();
        if (expectDrops && dropped > 0) return dropped;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return proc.getExperimentDroppedFrameCount();
}

} // namespace

int main()
{
    mib::test::Watchdog wd(30);

    // --- Case 1: experiment active, reader falls behind -> drops are counted ---
    wd.mark("experiment: reader falls behind");
    {
        auto store = std::make_shared<FrameStore>(32); // tiny ring
        ProcessingService proc;
        proc.setProcessingConfig(detectAny());
        proc.setRealtimeProcessingMode(ProcessingService::RealtimeProcessingMode::Inline);
        proc.setMonitoringActive(true);
        proc.startRealtime(store);
        proc.setRealtimeEnabled(true);
        proc.startExperiment(); // drop-frames is ignored while an experiment runs

        const uint64_t dropped = floodAndObserve(*store, proc, 6000, /*expectDrops=*/true, 5.0);

        proc.setRealtimeEnabled(false);
        proc.stopRealtime();
        const uint64_t finalDropped = proc.getExperimentDroppedFrameCount();
        proc.endExperiment();

        std::printf("experiment fall-behind: dropped=%llu (final=%llu)\n",
                    static_cast<unsigned long long>(dropped),
                    static_cast<unsigned long long>(finalDropped));
        MIB_EXPECT(dropped > 0,
                   "frames skipped during an experiment are counted, not dropped silently");
        MIB_EXPECT(finalDropped >= dropped,
                   "the experiment dropped-frame count is retained through stop for reporting");
    }

    // --- Case 2: no experiment -> counter stays zero (gated on experimentActive_) ---
    wd.mark("no experiment: counter stays zero");
    {
        auto store = std::make_shared<FrameStore>(32);
        ProcessingService proc;
        proc.setProcessingConfig(detectAny());
        proc.setRealtimeProcessingMode(ProcessingService::RealtimeProcessingMode::Inline);
        proc.setMonitoringActive(true);
        proc.startRealtime(store);
        proc.setRealtimeEnabled(true);
        // No startExperiment(): drop-frames defaults ON, and the fall-behind
        // counter must not increment outside an experiment.

        const uint64_t dropped = floodAndObserve(*store, proc, 6000, /*expectDrops=*/false, 1.5);

        proc.setRealtimeEnabled(false);
        proc.stopRealtime();

        std::printf("no-experiment flood: experiment_dropped=%llu (expect 0)\n",
                    static_cast<unsigned long long>(dropped));
        MIB_EXPECT(dropped == 0,
                   "the experiment dropped-frame counter only tracks losses during an experiment");
    }

    if (mib::test::exitCode() == 0) {
        std::printf("experiment dropped-frame accounting OK\n");
    }
    return mib::test::exitCode();
}

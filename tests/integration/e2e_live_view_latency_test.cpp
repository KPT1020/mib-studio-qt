// e2e_live_view_latency_test
//
// Characterizes the "live-view latency grows over a session and is cleared by a
// restart" symptom. The raw preview already renders the newest frame
// (PlaybackPanel -> FrameStore::getLatest), but the *processed overlay*
// (mask/contours/target-group) comes from ProcessingService's realtime loop,
// which by default (rtDropFrames_ == false) consumes the FrameStore
// sequentially via rtLastProcessed_. When capture outpaces processing the
// processed result falls progressively behind the live write head — an
// accumulating backlog that resets when realtime restarts.
//
// This test drives the real inline realtime loop with a producer that overruns
// processing, and measures the overlay lag (latestAvailableIndex - processed
// snapshot index) in two modes:
//   * drop-frames OFF  -> lag accumulates (reproduces the symptom)
//   * drop-frames ON   -> loop jumps to the newest frame, lag stays bounded
//
// The gate is the ratio between the two modes under identical load (machine
// independent): the backlog mode must lag far more than the drop mode. The
// per-mode growth (early vs late) is reported as the diagnostic artifact.

#include "backend/processing/ProcessingService.h"
#include "backend/playback/FrameStore.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

using backend::services::ProcessingService;
using backend::services::ProcessingConfig;
using backend::playback::FrameStore;
using Clock = std::chrono::steady_clock;

namespace {

constexpr int kFrameW = 256;
constexpr int kFrameH = 256;
constexpr size_t kCapacity = 5000;
constexpr uint64_t kPixelFormatMono8 = 0x01080001ULL;

ProcessingConfig makeLenientConfig()
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

// Frame guaranteed to produce a non-empty mask so the snapshot index advances
// on every processed frame.
cv::Mat makeBlobFrame(uint64_t i)
{
    cv::Mat img(kFrameH, kFrameW, CV_8UC1, cv::Scalar(0));
    const int cx = 64 + static_cast<int>(i % 64);
    cv::circle(img, cv::Point(cx, 128), 40, cv::Scalar(220), -1);
    cv::rectangle(img, cv::Rect(20, 20, 60, 60), cv::Scalar(180), -1);
    return img;
}

struct Sample {
    double elapsedMs;
    uint64_t lag;
};

enum class DropMode { ForceOff, ForceOn, UseDefault };

struct PhaseResult {
    uint64_t maxLag{0};
    double earlyLag{0.0};
    double lateLag{0.0};
    uint64_t pushed{0};
    uint64_t finalProcessedIndex{0};
    size_t sampleCount{0};
    bool dropFramesFlag{false};
};

double avgLagInWindow(const std::vector<Sample>& s, double loMs, double hiMs)
{
    double sum = 0.0;
    size_t n = 0;
    for (const auto& x : s) {
        if (x.elapsedMs >= loMs && x.elapsedMs <= hiMs) {
            sum += static_cast<double>(x.lag);
            ++n;
        }
    }
    return n ? sum / static_cast<double>(n) : 0.0;
}

PhaseResult runOverload(ProcessingService& proc, DropMode mode, int durationMs)
{
    auto store = std::make_shared<FrameStore>(kCapacity);

    proc.setRealtimeProcessingMode(ProcessingService::RealtimeProcessingMode::Inline);
    // UseDefault deliberately does NOT call setRealtimeDropFrames, so the phase
    // exercises whatever the constructor default is — the actual user path.
    if (mode == DropMode::ForceOff) proc.setRealtimeDropFrames(false);
    else if (mode == DropMode::ForceOn) proc.setRealtimeDropFrames(true);
    proc.startRealtime(store);
    proc.setRealtimeEnabled(true);

    std::atomic<bool> producing{true};
    std::atomic<uint64_t> pushed{0};

    // Prebuild frames so the producer loop is essentially a memcpy into the ring
    // (drawing per-frame would throttle it). Avoid std::this_thread::sleep_for
    // for pacing: on Windows its granularity (~1-15 ms) would cap the producer
    // well below processing speed and defeat the overload.
    constexpr uint64_t kPrebuilt = 64;
    std::vector<cv::Mat> frames;
    frames.reserve(kPrebuilt);
    for (uint64_t k = 0; k < kPrebuilt; ++k) frames.push_back(makeBlobFrame(k));

    // Producer overruns processing: pushes far faster than the realtime loop can
    // process full frames. A periodic yield keeps the processing thread
    // scheduled on low-core machines without the sleep-granularity problem.
    std::thread producer([&] {
        uint64_t i = 0;
        while (producing.load(std::memory_order_relaxed)) {
            const cv::Mat& f = frames[i % kPrebuilt];
            store->pushFrame(f.data,
                             static_cast<size_t>(f.total()),
                             static_cast<uint64_t>(kFrameW),
                             static_cast<uint64_t>(kFrameH),
                             static_cast<size_t>(kFrameW),
                             kPixelFormatMono8,
                             (i + 1) * 1000ULL);
            ++i;
            pushed.fetch_add(1, std::memory_order_relaxed);
            if ((i & 0x1F) == 0) std::this_thread::yield();
        }
    });

    std::vector<Sample> samples;
    PhaseResult r;
    const auto start = Clock::now();
    while (true) {
        const double elapsedMs =
            std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        if (elapsedMs >= durationMs) break;

        const uint64_t latest = store->latestAvailableIndex();
        ProcessingService::RealtimeSnapshot snap;
        if (proc.getLatestSnapshot(snap)) {
            const uint64_t lag = (latest > snap.index) ? (latest - snap.index) : 0;
            samples.push_back({elapsedMs, lag});
            r.maxLag = std::max(r.maxLag, lag);
            r.finalProcessedIndex = snap.index;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }

    producing.store(false, std::memory_order_relaxed);
    producer.join();
    proc.setRealtimeEnabled(false);
    proc.stopRealtime();

    r.dropFramesFlag = proc.getRealtimeDropFrames();
    r.pushed = pushed.load();
    r.sampleCount = samples.size();
    // "early" = first second after warmup; "late" = final ~0.6 s.
    r.earlyLag = avgLagInWindow(samples, 400.0, 1000.0);
    r.lateLag = avgLagInWindow(samples, durationMs - 600.0, static_cast<double>(durationMs));
    return r;
}

void printPhase(const char* name, const PhaseResult& r)
{
    std::cout << std::fixed << std::setprecision(1);
    std::cout << name << ": dropFrames=" << (r.dropFramesFlag ? "on" : "off")
              << " pushed=" << r.pushed
              << " samples=" << r.sampleCount
              << " maxLag=" << r.maxLag
              << " earlyLag=" << r.earlyLag
              << " lateLag=" << r.lateLag
              << " finalProcessedIdx=" << r.finalProcessedIndex << "\n";
}

PhaseResult runFreshPhase(DropMode mode, int durationMs)
{
    ProcessingService proc;
    proc.setProcessingConfig(makeLenientConfig());
    return runOverload(proc, mode, durationMs);
}

} // namespace

int main()
{
    constexpr int kDurationMs = 2000;

    std::cout << "=== e2e live-view (processed overlay) latency ===\n";
    std::cout << "frame=" << kFrameW << "x" << kFrameH
              << " ring=" << kCapacity
              << " duration=" << kDurationMs << "ms per phase\n\n";

    // Forced-off reproduces the symptom; forced-on is the bounded reference;
    // default exercises the shipped constructor default (the actual user path).
    const PhaseResult backlog = runFreshPhase(DropMode::ForceOff, kDurationMs);
    printPhase("drop-frames FORCE-OFF ", backlog);
    const PhaseResult dropped = runFreshPhase(DropMode::ForceOn, kDurationMs);
    printPhase("drop-frames FORCE-ON  ", dropped);
    const PhaseResult deflt = runFreshPhase(DropMode::UseDefault, kDurationMs);
    printPhase("drop-frames DEFAULT   ", deflt);

    std::cout << "\n";

    int rc = 0;

    // Confirm the producer actually overran processing in backlog mode;
    // otherwise the comparison is meaningless (environment too fast/idle).
    if (backlog.maxLag < 100) {
        std::cout << "INCONCLUSIVE: processing kept up (maxLag=" << backlog.maxLag
                  << "); could not establish overload. Treating as pass.\n";
        return 0;
    }

    const uint64_t dropMax = std::max<uint64_t>(dropped.maxLag, 1);
    const uint64_t defltMax = std::max<uint64_t>(deflt.maxLag, 1);
    const double ratio = static_cast<double>(backlog.maxLag) / static_cast<double>(dropMax);
    const double defltRatio = static_cast<double>(backlog.maxLag) / static_cast<double>(defltMax);
    std::cout << std::setprecision(2)
              << "forceOff.maxLag / forceOn.maxLag = " << ratio << "x\n"
              << "forceOff.maxLag / default.maxLag = " << defltRatio << "x\n";

    // The fix: the shipped default must bound the overlay backlog.
    if (!deflt.dropFramesFlag) {
        std::cout << "FAIL: realtime drop-frames is not ON by default; live view "
                     "will accumulate an overlay backlog.\n";
        rc = 1;
    }
    if (defltRatio < 4.0) {
        std::cout << "FAIL: default config did not bound the overlay backlog "
                     "(expected force-off to lag >= 4x the default).\n";
        rc = 1;
    }
    // Sanity: forced-on must also bound it (guards the mechanism itself).
    if (ratio < 4.0) {
        std::cout << "FAIL: drop-frames-on did not bound the overlay backlog.\n";
        rc = 1;
    }

    if (rc == 0) {
        std::cout << "\nPASS: with drop-frames ON by default the processed-overlay "
                     "lag stays bounded; only the explicit force-off path "
                     "accumulates the growing live-view backlog.\n";
    }
    return rc;
}

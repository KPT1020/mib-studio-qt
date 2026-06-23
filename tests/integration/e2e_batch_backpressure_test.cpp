// e2e_batch_backpressure_test
//
// Performance/safety guard for the async batch pipeline under overload. Floods
// the pipeline far faster than it can process and asserts:
//   * the queue is BOUNDED (maxQueueDepth <= configured maxQueuedFrames) — no
//     unbounded memory growth (this is what caused the live-view backlog class),
//   * frame accounting is conserved (offered == accepted + rejected), and
//   * every accepted frame is processed (no silent loss of accepted work).

#include "backend/processing/ProcessingService.h"

#include "support/assert.h"
#include "support/watchdog.h"

#include <opencv2/core.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

using backend::services::ProcessingService;
using backend::services::ProcessingConfig;
using backend::services::ProcessedFrame;

int main()
{
    mib::test::Watchdog wd(60);
    wd.mark("start");

    constexpr size_t kMaxQueued = 64;
    constexpr uint64_t kOffered = 8000;

    ProcessingService proc;
    ProcessingConfig cfg;  // defaults are fine; we only care about flow control
    cfg.enable_border_check = false;
    cfg.enable_area_range_check = false;
    cfg.enable_ring_ratio_check = false;
    cfg.require_single_inner_contour = false;

    std::atomic<uint64_t> callbackProcessed{0};

    ProcessingService::BatchPipelineConfig bcfg;
    bcfg.batchSize = 8;
    bcfg.maxQueuedFrames = kMaxQueued;
    bcfg.workerCount = 2;
    bcfg.processing = cfg;

    const bool started = proc.startBatchPipeline(
        bcfg, [&](std::vector<ProcessedFrame> batch) {
            callbackProcessed.fetch_add(batch.size(), std::memory_order_relaxed);
        });
    MIB_REQUIRE(started, "batch pipeline started");

    const cv::Mat frame(96, 96, CV_8UC1, cv::Scalar(128));
    uint64_t accepted = 0, rejected = 0;
    for (uint64_t i = 0; i < kOffered; ++i) {
        wd.mark("enqueue");
        if (proc.enqueueBatchFrame(frame, i, i * 1000ULL)) {
            ++accepted;
        } else {
            ++rejected;  // queue full -> backpressure rejected it (bounded memory)
        }
    }

    // Drain.
    wd.mark("drain");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto s = proc.getBatchPipelineStats();
        if (s.framesProcessed >= accepted) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    const auto stats = proc.getBatchPipelineStats();
    proc.stopBatchPipeline();

    std::printf("offered=%llu accepted=%llu rejected=%llu processed=%llu "
                "maxQueueDepth=%zu maxQueued=%zu\n",
                (unsigned long long)kOffered, (unsigned long long)accepted,
                (unsigned long long)rejected, (unsigned long long)stats.framesProcessed,
                stats.maxQueueDepth, kMaxQueued);

    // Conservation: every offered frame was either accepted or rejected.
    MIB_EXPECT(accepted + rejected == kOffered, "offered == accepted + rejected");
    // Backpressure actually engaged (we overran the queue).
    MIB_EXPECT(rejected > 0, "backpressure rejected frames under overload");
    // Bounded memory: the queue never exceeded its configured cap.
    MIB_EXPECT(stats.maxQueueDepth <= kMaxQueued,
               "queue depth stayed within the configured bound");
    // No silent loss of accepted work.
    MIB_EXPECT(stats.framesProcessed == accepted,
               "every accepted frame was processed");

    if (mib::test::exitCode() == 0) {
        std::printf("batch pipeline applies bounded backpressure with conserved accounting\n");
    }
    return mib::test::exitCode();
}

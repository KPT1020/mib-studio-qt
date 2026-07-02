// Dynamic verification for the dangling-reference race fixed in
// ProcessingService::realtimeBatchLoop(): that function declares
// callbackValid/callbackInvalid as stack-local atomics, captured BY
// REFERENCE by a lambda handed to startBatchPipeline() and executed on
// batch-worker threads. Before the fix, cleanup (stopBatchPipeline(), which
// joins those workers) only ran on the loop's NORMAL exit path — an
// exception thrown from inside the per-frame driving loop unwound past that
// join, destroying the atomics while a worker could still be executing the
// callback that references them.
//
// processing_fault_injection_test.cpp does not catch this: it drives
// startBatchPipeline() directly with a throwing RESULT CALLBACK, never the
// real startRealtime()/realtimeLoop()/realtimeBatchLoop() driving loop. This
// test drives that real path and throws from inside the loop itself (via a
// test-only fault hook — enqueueBatchFrame(Frame) no longer throws on bad
// geometry after the frame-buffer-validation hardening, so there is no
// naturally reachable throw site left to exercise this from valid input
// alone) with the batch pipeline's workers concurrently and repeatedly
// invoking the real callback. Run under ThreadSanitizer, this either shows
// the RAII guard closed the race (clean run to completion) or resurfaces it
// (TSan use-after-scope / heap-use-after-free report).

#include "backend/processing/ProcessingService.h"
#include "backend/playback/FrameStore.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

backend::services::ProcessingConfig makeConfig() {
    backend::services::ProcessingConfig config;
    config.gaussian_blur_size = 1;
    config.morph_kernel_size = 1;
    config.morph_iterations = 1;
    config.bg_subtract_threshold = 127;
    config.empty_frame_pixel_threshold = 1;
    return config;
}

bool waitFor(const std::function<bool()>& condition, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return condition();
}

} // namespace

int main() {
    using namespace std::chrono_literals;

    auto frameStore = std::make_shared<backend::playback::FrameStore>(64);

    // Feed the store from a separate thread so the realtime loop has real
    // frames to drive through the batch pipeline concurrently with the
    // fault injection below.
    std::atomic<bool> stopProducer{false};
    std::thread producer([&] {
        constexpr uint64_t w = 16, h = 16;
        std::vector<uint8_t> buf(w * h, 0x42);
        while (!stopProducer.load()) {
            frameStore->pushFrame(buf.data(), buf.size(), w, h, /*linePitch=*/0,
                                  /*pixelFormat=*/0x01080001, /*timestamp=*/0);
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    });

    backend::services::ProcessingService service;
    service.setRealtimeProcessingMode(
        backend::services::ProcessingService::RealtimeProcessingMode::AsyncBatch);
    backend::services::ProcessingService::RealtimeBatchSettings batchSettings;
    batchSettings.batchSize = 4;
    batchSettings.workerCount = 2;
    batchSettings.maxBatchDelayMs = 2;
    service.setRealtimeBatchSettings(batchSettings);
    service.setProcessingConfig(makeConfig());

    // Throw from inside realtimeBatchLoop's own driving loop on every Nth
    // frame. If the fix regressed, a batch worker mid-callback when this
    // unwinds the loop would reference destroyed stack memory -- exactly
    // the race TSan is positioned to catch.
    std::atomic<uint64_t> hookCalls{0};
    service.setTestOnlyRealtimeBatchFaultHook([&](uint64_t idx) {
        const uint64_t n = hookCalls.fetch_add(1) + 1;
        if (n % 7 == 0) {
            throw std::runtime_error("injected realtimeBatchLoop fault at idx=" + std::to_string(idx));
        }
    });

    service.startRealtime(frameStore);

    // Let the loop run through several throw/restart cycles under load.
    const bool ranLongEnough = waitFor([&] { return hookCalls.load() > 200; }, 10s);

    stopProducer.store(true);
    producer.join();

    // stopRealtime() must complete cleanly -- if the guard fix regressed
    // and corrupted state, this join (or a prior TSan report) is where it
    // would surface.
    service.stopRealtime();

    if (!ranLongEnough) {
        std::cerr << "realtime fault-injection loop did not reach the expected "
                     "throw count (hookCalls=" << hookCalls.load() << ") within timeout\n";
        return 1;
    }

    std::cout << "processing realtime fault-injection test passed ("
              << hookCalls.load() << " hook invocations, service survived "
                 "repeated in-loop exceptions)\n";
    return 0;
}

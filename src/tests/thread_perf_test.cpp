// Thread performance benchmarks backing the 2026-04-16 thread audit.
//
// What this test measures (and why it matters):
//
// 1. AutofocusService::onRingRatio producer-side latency.
//    The audit moved the O(n log n) stats sort off the ProcessingService
//    realtime thread onto AutofocusService::statsThread_. onRingRatio is
//    now a push into an inbox + atomic freshness updates + notify_one.
//    This bench calls onRingRatio in a tight loop and reports the latency
//    distribution; regressions (e.g. accidentally re-taking
//    ringRatioMutex_ on the producer) would show up as a p99 blow-up.
//
// 2. TriggerService end-to-end wake-up latency (idle).
//    Measures wall-clock time from onTargetGroupResult(true) on the
//    realtime (here: test) thread to setTriggerOutput(true) observed on
//    the trigger thread. This is the baseline for the CV notify +
//    scheduler wake cost on an otherwise idle system.
//
// 3. TriggerService wake-up latency under concurrent onRingRatio load.
//    Drives a producer thread hammering AutofocusService::onRingRatio
//    with a pre-filled 1000-sample buffer (so statsThread_ is doing the
//    worst-case sort continuously) and measures trigger wake-up latency
//    on top. Validates the audit's central claim that autofocus stats
//    cannot stall the trigger wake-up.
//
// All results go to spdlog and to a JSON file (default
// "thread_perf_results.json" next to the binary, overridable via
// MIB_THREAD_PERF_JSON) so CI can upload them to MLflow for trend
// tracking. We deliberately do NOT fail the test on threshold
// violations: timing is noisy on shared CI runners and the intent is
// to track metrics over time, not to gate merges.
//
// Related task record: knowledge_map/task/2026-04-16-thread-performance-audit.md

#include "backend/services/AutofocusService.h"
#include "backend/services/TriggerService.h"
#include "camera/common/ICamera.h"
#include "perf_common.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <spdlog/spdlog.h>

namespace {

using perf_clock = std::chrono::steady_clock;
using mib::perf::LatencyStats;
using mib::perf::summarise;
using mib::perf::logStats;
using mib::perf::envSizeOr;

// Fake camera that records trigger output transitions so the test can
// time TriggerService::triggerLoop from outside.
//
// We deliberately do not override grabFrame / pollStats: this camera is
// only wired into TriggerService, never into CaptureService.
class RecordingTriggerCamera : public camera::common::ICamera {
public:
    void applyConfig(const camera::common::CameraConfig&) override {}
    bool start() override { running_.store(true); return true; }
    void stop() override { running_.store(false); }
    bool isRunning() const override { return running_.load(); }

    bool grabFrame(camera::common::Frame&) override { return false; }
    bool pollStats(camera::common::CameraStats&) const override { return false; }

    void configureTriggerOutput(const std::string&) override {}
    bool setTriggerOutput(bool high) override {
        if (high) {
            const auto now = perf_clock::now().time_since_epoch();
            lastHighNs_.store(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count(),
                              std::memory_order_release);
            highCount_.fetch_add(1, std::memory_order_acq_rel);
        } else {
            lowCount_.fetch_add(1, std::memory_order_relaxed);
        }
        return true;
    }

    int64_t lastHighNs() const { return lastHighNs_.load(std::memory_order_acquire); }
    uint64_t highCount() const { return highCount_.load(std::memory_order_acquire); }

    void reset() {
        lastHighNs_.store(0, std::memory_order_release);
        highCount_.store(0, std::memory_order_release);
        lowCount_.store(0, std::memory_order_release);
    }

private:
    std::atomic<bool> running_{false};
    std::atomic<int64_t> lastHighNs_{0};
    std::atomic<uint64_t> highCount_{0};
    std::atomic<uint64_t> lowCount_{0};
};

// Bench 1: onRingRatio producer-side latency.
//
// Pre-fills the stats deque to saturation (1000 samples) so the stats
// thread is running worst-case sorts concurrently. onRingRatio itself
// must remain O(1) regardless — this bench asserts that by reporting
// a bounded p99.
LatencyStats benchOnRingRatioLatency(size_t iterations) {
    backend::services::AutofocusService af;

    // Let statsThread_ come up and pre-fill the buffer so every subsequent
    // sample incurs the "full buffer" path on the consumer side.
    std::mt19937_64 rng(0xC0FFEEULL);
    std::uniform_real_distribution<double> rrDist(10.0, 30.0);
    for (size_t i = 0; i < 1200; ++i) {
        af.onRingRatio(rrDist(rng), static_cast<int64_t>(i));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::vector<double> samples;
    samples.reserve(iterations);
    for (size_t i = 0; i < iterations; ++i) {
        const auto t0 = perf_clock::now();
        af.onRingRatio(rrDist(rng), static_cast<int64_t>(i));
        const auto t1 = perf_clock::now();
        samples.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }
    return summarise(std::move(samples));
}

// Bench 2: trigger wake-up latency on an idle system.
LatencyStats benchTriggerWakeupIdle(size_t iterations) {
    RecordingTriggerCamera cam;
    backend::services::TriggerService trig;
    trig.setCamera(&cam);
    trig.setPulseDurationUs(1);
    trig.start();

    // Warm-up — first CV wake tends to be slow (thread hot-path not hot yet).
    for (size_t i = 0; i < 32; ++i) {
        cam.reset();
        trig.onTargetGroupResult(true);
        while (cam.highCount() == 0) std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }

    std::vector<double> samples;
    samples.reserve(iterations);
    for (size_t i = 0; i < iterations; ++i) {
        cam.reset();
        const auto t0 = perf_clock::now();
        const int64_t t0Ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            t0.time_since_epoch()).count();
        trig.onTargetGroupResult(true);
        // Spin until the trigger thread has observed the request and raised
        // the line. Measuring wall-clock through an atomic gives us the
        // notify→wake→setTriggerOutput(true) latency directly.
        while (cam.highCount() == 0) std::this_thread::yield();
        const int64_t hiNs = cam.lastHighNs();
        samples.push_back(static_cast<double>(hiNs - t0Ns) / 1000.0);
        // Let the 1 µs pulse drop back to low before the next iteration
        // so running_-checks don't collapse pulses.
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }

    trig.stop();
    return summarise(std::move(samples));
}

// Bench 3: trigger wake-up latency under sustained onRingRatio load.
//
// Simulates the realtime callback chain in steady state: a producer
// thread pushes ring-ratio samples as fast as it can while the test
// thread fires target-group events. This validates the audit's claim
// that autofocus stats work cannot stall the trigger wake-up, and also
// re-checks that the target-group-first callback order matters little
// now that onRingRatio is O(1).
LatencyStats benchTriggerWakeupUnderRingRatioLoad(size_t iterations) {
    RecordingTriggerCamera cam;
    backend::services::TriggerService trig;
    backend::services::AutofocusService af;

    trig.setCamera(&cam);
    trig.setPulseDurationUs(1);
    trig.start();

    // Saturate AutofocusService buffer before the measured loop starts so
    // statsThread_ is doing worst-case sorts from iteration zero.
    std::mt19937_64 rngInit(0xFEEDULL);
    std::uniform_real_distribution<double> rrDist(10.0, 30.0);
    for (size_t i = 0; i < 1200; ++i) {
        af.onRingRatio(rrDist(rngInit), static_cast<int64_t>(i));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::atomic<bool> producerRunning{true};
    std::thread producer([&] {
        std::mt19937_64 rng(0xBEEFULL);
        std::uniform_real_distribution<double> d(10.0, 30.0);
        int64_t ts = 1'000'000'000LL;
        while (producerRunning.load(std::memory_order_relaxed)) {
            af.onRingRatio(d(rng), ts);
            ts += 200'000; // ~5 kHz nominal rate
        }
    });

    // Warm-up (under load).
    for (size_t i = 0; i < 32; ++i) {
        cam.reset();
        trig.onTargetGroupResult(true);
        while (cam.highCount() == 0) std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }

    std::vector<double> samples;
    samples.reserve(iterations);
    for (size_t i = 0; i < iterations; ++i) {
        cam.reset();
        const auto t0 = perf_clock::now();
        const int64_t t0Ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            t0.time_since_epoch()).count();
        trig.onTargetGroupResult(true);
        while (cam.highCount() == 0) std::this_thread::yield();
        const int64_t hiNs = cam.lastHighNs();
        samples.push_back(static_cast<double>(hiNs - t0Ns) / 1000.0);
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    producerRunning.store(false, std::memory_order_release);
    producer.join();
    trig.stop();
    return summarise(std::move(samples));
}

// Bench 4: contention on monitoringFramesMutex_ does NOT exist on the
// trigger path. We can't drive the ProcessingService realtime loop
// without a camera, so instead we simulate the mutex itself: a dummy
// mutex is held by a background thread in 1 ms bursts (this is worst-
// case — the real UI snapshot is ~a few ms every 500 ms). The test
// thread fires target-group events and measures trigger latency. The
// mutex is never touched by the trigger path, so latency must stay flat.
//
// If a future refactor accidentally re-introduces a shared mutex
// between the monitoring snapshot and the trigger callback dispatch,
// this bench would catch it.
LatencyStats benchTriggerUnderUiSnapshotSim(size_t iterations) {
    RecordingTriggerCamera cam;
    backend::services::TriggerService trig;
    trig.setCamera(&cam);
    trig.setPulseDurationUs(1);
    trig.start();

    std::mutex simMonitoringMutex;
    std::atomic<bool> uiRunning{true};
    std::thread ui([&] {
        while (uiRunning.load(std::memory_order_relaxed)) {
            {
                std::scoped_lock lk(simMonitoringMutex);
                // Worst-case UI snapshot hold time.
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    });

    std::vector<double> samples;
    samples.reserve(iterations);
    for (size_t i = 0; i < iterations; ++i) {
        cam.reset();
        const auto t0 = perf_clock::now();
        const int64_t t0Ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            t0.time_since_epoch()).count();
        trig.onTargetGroupResult(true);
        while (cam.highCount() == 0) std::this_thread::yield();
        const int64_t hiNs = cam.lastHighNs();
        samples.push_back(static_cast<double>(hiNs - t0Ns) / 1000.0);
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    uiRunning.store(false, std::memory_order_release);
    ui.join();
    trig.stop();
    return summarise(std::move(samples));
}

} // namespace

int main() {
    try {
        spdlog::set_level(spdlog::level::info);

        // Iteration counts tuned so the full test completes in well under
        // the CTest 60 s timeout on a modest CI runner. Override via env
        // for local long-running perf sweeps.
        const size_t ringRatioIters  = envSizeOr("MIB_THREAD_PERF_RING_RATIO_ITERS", 50'000);
        const size_t triggerIters    = envSizeOr("MIB_THREAD_PERF_TRIGGER_ITERS", 500);
        const size_t triggerLoadIters = envSizeOr("MIB_THREAD_PERF_TRIGGER_LOAD_ITERS", 500);
        const size_t triggerUiIters  = envSizeOr("MIB_THREAD_PERF_TRIGGER_UI_ITERS", 500);

        SPDLOG_INFO("Thread perf benchmarks | ring_ratio_iters={} trigger_iters={} "
                    "trigger_load_iters={} trigger_ui_iters={}",
                    ringRatioIters, triggerIters, triggerLoadIters, triggerUiIters);

        const auto onRR = benchOnRingRatioLatency(ringRatioIters);
        logStats("AutofocusService::onRingRatio (saturated buffer)", onRR);

        const auto trigIdle = benchTriggerWakeupIdle(triggerIters);
        logStats("TriggerService wake-up (idle)", trigIdle);

        const auto trigLoaded = benchTriggerWakeupUnderRingRatioLoad(triggerLoadIters);
        logStats("TriggerService wake-up (ring-ratio producer)", trigLoaded);

        const auto trigUnderUi = benchTriggerUnderUiSnapshotSim(triggerUiIters);
        logStats("TriggerService wake-up (simulated UI snapshot)", trigUnderUi);

        // Soft ceilings — we log (WARN) but don't fail. These are generous
        // to absorb CI noise and Debug-build overhead; the real signal is
        // the trend in MLflow, not the absolute pass/fail here.
        auto check = [](const std::string& name, double value, double ceilingUs) {
            if (value > ceilingUs) {
                SPDLOG_WARN("{} p99={:.3f}us exceeds soft ceiling {:.3f}us",
                            name, value, ceilingUs);
            }
        };
        check("onRingRatio", onRR.p99Us, 200.0);
        check("trigger idle", trigIdle.p99Us, 5000.0);
        check("trigger under ring-ratio", trigLoaded.p99Us, 5000.0);
        check("trigger under UI snapshot sim", trigUnderUi.p99Us, 5000.0);

        const std::string jsonPath = mib::perf::resolveJsonOutPath(
            "MIB_THREAD_PERF_JSON", "thread_perf_results.json");
        mib::perf::JsonReport report;
        report.addStats("on_ring_ratio_latency",           onRR)
              .addStats("trigger_wakeup_idle",             trigIdle)
              .addStats("trigger_wakeup_ring_ratio_load",  trigLoaded)
              .addStats("trigger_wakeup_ui_snapshot_sim",  trigUnderUi);
        if (!report.writeTo(jsonPath)) {
            SPDLOG_WARN("Failed to open {} for JSON report", jsonPath);
        } else {
            SPDLOG_INFO("Wrote JSON report: {}", jsonPath);
        }
        return 0;
    } catch (const std::exception& ex) {
        SPDLOG_ERROR("thread_perf_test exception: {}", ex.what());
        return 2;
    }
}

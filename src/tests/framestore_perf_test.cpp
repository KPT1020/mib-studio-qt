// FrameStore performance benchmarks.
//
// FrameStore is the shared ring buffer between the capture thread
// (push) and the realtime + UI + recording threads (get). It uses a
// single std::mutex covering all ops. The 2026-04-16 thread audit
// explicitly flagged this as a future jitter source:
//
//   "FrameStore::mutex_ is a single std::mutex shared by push (capture
//    thread) and query (realtime/UI/frame-recording). Contention is
//    bounded by small per-call hold times, but a reader/writer lock or
//    a lock-free ring would reduce jitter at high fps."
//
// This test measures the current baseline so any future attempt to
// swap the mutex strategy has a concrete number to beat — and any
// regression introduced by code changes shows up in the MLflow trend.
//
// Four benches, all on a 5000-capacity ring (matching AppBackend's
// production setting):
//
//  1. pushFrame single-threaded, at 3 frame sizes.
//  2. getLatest single-threaded, full ring.
//  3. getByWriteIndex vs getByWriteIndexROI single-threaded — ROI should
//     be measurably cheaper since it skips the full data copy.
//  4. push + getLatest contention: dedicated producer + consumer
//     threads for 1 s; report latency percentiles for each.
//
// No camera, no Qt event loop, no services — just the FrameStore.

#include "backend/playback/FrameStore.h"
#include "perf_common.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <spdlog/spdlog.h>

using mib::perf::LatencyStats;
using mib::perf::summarise;
using mib::perf::logStats;
using mib::perf::envSizeOr;
using perf_clock = std::chrono::steady_clock;

namespace {

constexpr std::size_t kRingCapacity = 5000; // matches AppBackend::initialize

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

std::vector<uint8_t> makeSyntheticBuffer(int w, int h) {
    std::vector<uint8_t> buf(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    // Deterministic-ish fill so the copy actually touches memory (no zero-page optimisations).
    std::mt19937 rng(0x51ECu);
    for (std::size_t i = 0; i < buf.size(); ++i) {
        buf[i] = static_cast<uint8_t>(rng() & 0xFFu);
    }
    return buf;
}

// Bench 1 — single-threaded pushFrame latency.
LatencyStats benchPushLatency(const FrameSize& fs, std::size_t iterations) {
    backend::playback::FrameStore store(kRingCapacity);
    const auto buffer = makeSyntheticBuffer(fs.width, fs.height);

    // Warm up (fill the ring so subsequent pushes overwrite; matches steady state).
    for (std::size_t i = 0; i < kRingCapacity; ++i) {
        store.pushFrame(buffer.data(), buffer.size(),
                        static_cast<uint64_t>(fs.width),
                        static_cast<uint64_t>(fs.height),
                        static_cast<std::size_t>(fs.width),
                        0 /*mono8*/, static_cast<uint64_t>(i));
    }

    std::vector<double> samples;
    samples.reserve(iterations);
    for (std::size_t i = 0; i < iterations; ++i) {
        const auto t0 = perf_clock::now();
        store.pushFrame(buffer.data(), buffer.size(),
                        static_cast<uint64_t>(fs.width),
                        static_cast<uint64_t>(fs.height),
                        static_cast<std::size_t>(fs.width),
                        0, static_cast<uint64_t>(kRingCapacity + i));
        const auto t1 = perf_clock::now();
        samples.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }
    return summarise(std::move(samples));
}

// Bench 2 — single-threaded getLatest latency.
LatencyStats benchGetLatestLatency(const FrameSize& fs, std::size_t iterations) {
    backend::playback::FrameStore store(kRingCapacity);
    const auto buffer = makeSyntheticBuffer(fs.width, fs.height);

    for (std::size_t i = 0; i < kRingCapacity; ++i) {
        store.pushFrame(buffer.data(), buffer.size(),
                        static_cast<uint64_t>(fs.width),
                        static_cast<uint64_t>(fs.height),
                        static_cast<std::size_t>(fs.width),
                        0, static_cast<uint64_t>(i));
    }

    std::vector<double> samples;
    samples.reserve(iterations);
    backend::playback::Frame out;
    for (std::size_t i = 0; i < iterations; ++i) {
        const auto t0 = perf_clock::now();
        (void)store.getLatest(out);
        const auto t1 = perf_clock::now();
        samples.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }
    return summarise(std::move(samples));
}

// Bench 3a — getByWriteIndex full-frame copy.
LatencyStats benchGetByIndexLatency(const FrameSize& fs, std::size_t iterations) {
    backend::playback::FrameStore store(kRingCapacity);
    const auto buffer = makeSyntheticBuffer(fs.width, fs.height);
    for (std::size_t i = 0; i < kRingCapacity; ++i) {
        store.pushFrame(buffer.data(), buffer.size(),
                        static_cast<uint64_t>(fs.width),
                        static_cast<uint64_t>(fs.height),
                        static_cast<std::size_t>(fs.width),
                        0, static_cast<uint64_t>(i));
    }

    std::vector<double> samples;
    samples.reserve(iterations);
    backend::playback::Frame out;
    std::mt19937 rng(0xB00Bu);
    std::uniform_int_distribution<std::size_t> pick(0, kRingCapacity - 1);
    for (std::size_t i = 0; i < iterations; ++i) {
        const auto idx = pick(rng);
        const auto t0 = perf_clock::now();
        (void)store.getByWriteIndex(idx, out);
        const auto t1 = perf_clock::now();
        samples.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }
    return summarise(std::move(samples));
}

// Bench 3b — getByWriteIndexROI 25% ROI.
LatencyStats benchGetByIndexRoiLatency(const FrameSize& fs, std::size_t iterations) {
    backend::playback::FrameStore store(kRingCapacity);
    const auto buffer = makeSyntheticBuffer(fs.width, fs.height);
    for (std::size_t i = 0; i < kRingCapacity; ++i) {
        store.pushFrame(buffer.data(), buffer.size(),
                        static_cast<uint64_t>(fs.width),
                        static_cast<uint64_t>(fs.height),
                        static_cast<std::size_t>(fs.width),
                        0, static_cast<uint64_t>(i));
    }

    const int roiW = fs.width / 2;
    const int roiH = fs.height / 2;
    const int roiX = fs.width / 4;
    const int roiY = fs.height / 4;

    std::vector<double> samples;
    samples.reserve(iterations);
    backend::playback::Frame out;
    std::mt19937 rng(0xB00Cu);
    std::uniform_int_distribution<std::size_t> pick(0, kRingCapacity - 1);
    for (std::size_t i = 0; i < iterations; ++i) {
        const auto idx = pick(rng);
        const auto t0 = perf_clock::now();
        (void)store.getByWriteIndexROI(idx, roiX, roiY, roiW, roiH, out);
        const auto t1 = perf_clock::now();
        samples.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }
    return summarise(std::move(samples));
}

// Bench 4 — producer/consumer contention for a fixed wall-clock window.
struct ContentionResult {
    LatencyStats pushStats;
    LatencyStats getStats;
    double wallMs;
    std::uint64_t pushed;
    std::uint64_t gotten;
};

ContentionResult benchContention(const FrameSize& fs,
                                 std::chrono::milliseconds duration) {
    backend::playback::FrameStore store(kRingCapacity);
    const auto buffer = makeSyntheticBuffer(fs.width, fs.height);

    std::atomic<bool> running{true};
    std::vector<double> pushSamples;
    std::vector<double> getSamples;
    pushSamples.reserve(1u << 16);
    getSamples.reserve(1u << 16);

    // Guard per-thread sample vectors under their own mutex — the store is
    // the thing under test, not our reporting path.
    std::mutex pushMu, getMu;

    const auto t0 = perf_clock::now();

    std::thread producer([&] {
        std::uint64_t ts = 0;
        while (running.load(std::memory_order_relaxed)) {
            const auto a = perf_clock::now();
            store.pushFrame(buffer.data(), buffer.size(),
                            static_cast<uint64_t>(fs.width),
                            static_cast<uint64_t>(fs.height),
                            static_cast<std::size_t>(fs.width),
                            0, ts++);
            const auto b = perf_clock::now();
            const double us = std::chrono::duration<double, std::micro>(b - a).count();
            {
                std::scoped_lock lk(pushMu);
                pushSamples.push_back(us);
            }
        }
    });

    std::thread consumer([&] {
        backend::playback::Frame out;
        while (running.load(std::memory_order_relaxed)) {
            const auto a = perf_clock::now();
            (void)store.getLatest(out);
            const auto b = perf_clock::now();
            const double us = std::chrono::duration<double, std::micro>(b - a).count();
            {
                std::scoped_lock lk(getMu);
                getSamples.push_back(us);
            }
        }
    });

    std::this_thread::sleep_for(duration);
    running.store(false, std::memory_order_release);
    producer.join();
    consumer.join();

    const auto t1 = perf_clock::now();
    ContentionResult r;
    r.pushed = static_cast<std::uint64_t>(pushSamples.size());
    r.gotten = static_cast<std::uint64_t>(getSamples.size());
    r.pushStats = summarise(std::move(pushSamples));
    r.getStats = summarise(std::move(getSamples));
    r.wallMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return r;
}

} // namespace

int main() {
    try {
        spdlog::set_level(spdlog::level::info);

        const std::size_t pushIters = envSizeOr("MIB_FRAMESTORE_PUSH_ITERS", 2000);
        const std::size_t getIters  = envSizeOr("MIB_FRAMESTORE_GET_ITERS", 20'000);
        const auto contentionMs = std::chrono::milliseconds(
            static_cast<long long>(envSizeOr("MIB_FRAMESTORE_CONTENTION_MS", 1000)));

        SPDLOG_INFO("FrameStore perf | ring={} push_iters={} get_iters={} contention_ms={}",
                    kRingCapacity, pushIters, getIters, contentionMs.count());

        mib::perf::JsonReport report;

        for (const auto& fs : kSizes) {
            const auto push = benchPushLatency(fs, pushIters);
            logStats(std::string("push ") + fs.label, push);
            report.addStats(std::string("push_") + fs.label, push);

            const auto getLatest = benchGetLatestLatency(fs, getIters);
            logStats(std::string("getLatest ") + fs.label, getLatest);
            report.addStats(std::string("get_latest_") + fs.label, getLatest);

            const auto getByIdx = benchGetByIndexLatency(fs, getIters);
            logStats(std::string("getByWriteIndex ") + fs.label, getByIdx);
            report.addStats(std::string("get_by_write_index_") + fs.label, getByIdx);

            const auto getByIdxRoi = benchGetByIndexRoiLatency(fs, getIters);
            logStats(std::string("getByWriteIndexROI(25%) ") + fs.label, getByIdxRoi);
            report.addStats(std::string("get_by_write_index_roi_") + fs.label, getByIdxRoi);
        }

        // Contention on mid-size frames only — full 2048x2048 contention
        // pushes the test into multi-second territory on weaker runners.
        const auto contention = benchContention(kSizes[1], contentionMs);
        logStats("contention push 1024x1024", contention.pushStats);
        logStats("contention getLatest 1024x1024", contention.getStats);
        SPDLOG_INFO("contention | wall={:.1f} ms pushed={} gotten={} "
                    "push_tput={:.0f}/s get_tput={:.0f}/s",
                    contention.wallMs, contention.pushed, contention.gotten,
                    contention.pushed * 1000.0 / contention.wallMs,
                    contention.gotten * 1000.0 / contention.wallMs);
        report.addStats("contention_push_1024x1024", contention.pushStats)
              .addStats("contention_get_latest_1024x1024", contention.getStats)
              .addNumber("contention_wall_ms", contention.wallMs)
              .addInt("contention_pushed_count", static_cast<long long>(contention.pushed))
              .addInt("contention_gotten_count", static_cast<long long>(contention.gotten));

        // Soft ceilings — warn only.
        auto check = [](const std::string& name, double v, double c) {
            if (v > c) SPDLOG_WARN("{} p99={:.3f}us exceeds soft ceiling {:.3f}us", name, v, c);
        };
        // 2048x2048 = 4 MB memcpy, ~2 ms on DDR4. Be generous.
        check("push 2048x2048", benchPushLatency(kSizes[2], 100).p99Us, 20'000.0);

        const std::string jsonPath = mib::perf::resolveJsonOutPath(
            "MIB_FRAMESTORE_PERF_JSON", "framestore_perf_results.json");
        if (!report.writeTo(jsonPath)) {
            SPDLOG_WARN("Failed to open {} for JSON report", jsonPath);
        } else {
            SPDLOG_INFO("Wrote JSON report: {}", jsonPath);
        }
        return 0;
    } catch (const std::exception& ex) {
        SPDLOG_ERROR("framestore_perf_test exception: {}", ex.what());
        return 2;
    }
}

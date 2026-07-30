#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace backend::diagnostics {

// Gauges the recorder cannot see itself, supplied by the embedding
// application once per tick. Leave fields at their defaults when a gauge is
// not applicable (e.g. batch fields in inline mode).
struct PipelineTrendProviderSample {
    uint64_t latestAvailableIndex{0}; // FrameStore write head (absolute index)
    uint64_t rtLastProcessed{0};      // realtime consumer cursor
    double captureFps{0.0};
    double algoFps{0.0};
    size_t batchQueueDepth{0};
    size_t batchMaxQueueDepth{0};
    uint64_t batchAccepted{0};
    uint64_t batchDropped{0};
    uint64_t batchProcessed{0};
    int realtimeMode{0}; // 0 = inline, 1 = async-batch
    bool dropFrames{false};
    bool experimentActive{false};
};

// Periodic (default 1 Hz) time-series consumer of PipelineTimingRecorder.
//
// The recorder's rings retain only ~65k frames (~2 minutes at 500 fps), so a
// stop-time CSV dump cannot show how latency evolves over a long session.
// This sampler runs on its own normal-priority thread and appends one row per
// tick to <directory>/pipeline_trend.csv: windowed per-stage percentiles from
// summarize(), cumulative skip counters, inter-frame gap statistics on both
// clocks (host grab gap vs device tick gap, never mixed), process RSS, the
// always-on live target-latency gauges, and the provider gauges above.
//
// It only READS the recorder (its designed concurrent-read mode) and touches
// no pipeline hot path; summarize()/frameRecords() allocate and sort, which
// is why this work lives on a dedicated low-rate thread. Rows are flushed as
// written so a crash loses at most one tick.
class PipelineTrendSampler {
public:
    using Provider = std::function<PipelineTrendProviderSample()>;

    PipelineTrendSampler() = default;
    ~PipelineTrendSampler();
    PipelineTrendSampler(const PipelineTrendSampler&) = delete;
    PipelineTrendSampler& operator=(const PipelineTrendSampler&) = delete;

    // Create `directory` if needed and start appending to pipeline_trend.csv
    // inside it (file truncated per session). Returns false if the file
    // cannot be opened or the sampler is already running. `provider` may be
    // empty; provider columns are then zero.
    bool start(const std::string& directory, Provider provider,
               std::chrono::milliseconds interval = std::chrono::milliseconds(1000));

    // Signal the thread, join it (bounded: the loop waits at most one
    // interval), and flush/close the CSV. Safe to call when not running.
    void stop();

    bool isRunning() const { return running_; }

    // Cap on how many of the newest ring records feed each tick's percentile
    // window. ~8 s of frames at 500 fps: wide enough for stable p95s, small
    // enough that the off-thread sort stays cheap.
    static constexpr size_t kSummarySampleLimit = 4096;

private:
    void run();
    void writeHeader();
    void writeRow();

    Provider provider_;
    std::ofstream out_;
    std::chrono::milliseconds interval_{1000};
    std::chrono::steady_clock::time_point startTime_{};
    uint64_t lastFrameCount_{0};
    // Per-tid cumulative CPU seconds at the previous tick, for per-stage
    // CPU%% columns (tids from ThreadRegistry).
    std::unordered_map<uint64_t, double> threadCpuPrev_;
    std::chrono::steady_clock::time_point lastRowTime_{};

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stopRequested_{false};
    bool running_{false};
};

} // namespace backend::diagnostics

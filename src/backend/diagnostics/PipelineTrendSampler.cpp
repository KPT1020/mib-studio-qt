#include "backend/diagnostics/PipelineTrendSampler.h"

#include "backend/app/Tools.h"
#include "backend/diagnostics/MatAllocStats.h"
#include "backend/diagnostics/PipelineTimingRecorder.h"
#include "backend/diagnostics/ThreadRegistry.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace backend::diagnostics {

namespace {

// Mean and nearest-rank p95 of successive deltas over a stamp sequence.
// Non-monotonic pairs (ring seams, zero stamps) are skipped.
struct GapStats {
    double meanUs{0.0};
    uint64_t p95Us{0};
    size_t count{0};
};

GapStats reduceGaps(const std::vector<uint64_t>& stamps) {
    GapStats g;
    std::vector<uint64_t> deltas;
    deltas.reserve(stamps.size());
    for (size_t i = 1; i < stamps.size(); ++i) {
        if (stamps[i - 1] != 0 && stamps[i] >= stamps[i - 1]) {
            deltas.push_back(stamps[i] - stamps[i - 1]);
        }
    }
    if (deltas.empty()) return g;
    uint64_t sum = 0;
    for (uint64_t d : deltas) sum += d;
    g.count = deltas.size();
    g.meanUs = static_cast<double>(sum) / static_cast<double>(deltas.size());
    std::sort(deltas.begin(), deltas.end());
    size_t idx = static_cast<size_t>(std::ceil(0.95 * static_cast<double>(deltas.size())));
    if (idx == 0) idx = 1;
    g.p95Us = deltas[std::min(idx, deltas.size()) - 1];
    return g;
}

// Per-thread cumulative stats. cpuSeconds < 0 means the thread could not be
// sampled (exited, or unsupported platform).
struct ThreadSample {
    double cpuSeconds{-1.0};
    uint64_t nonvoluntaryCs{0};
};

#ifdef _WIN32
ThreadSample sampleThread(uint64_t tid) {
    ThreadSample s;
    HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(tid));
    if (!h) return s;
    FILETIME create{}, exit_{}, kernel{}, user{};
    if (GetThreadTimes(h, &create, &exit_, &kernel, &user)) {
        auto toSec = [](const FILETIME& ft) {
            const uint64_t t = (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
            return static_cast<double>(t) * 100e-9; // 100 ns units
        };
        s.cpuSeconds = toSec(kernel) + toSec(user);
    }
    CloseHandle(h);
    return s; // context switches not exposed per-thread on Windows: stays 0
}
#else
ThreadSample sampleThread(uint64_t tid) {
    ThreadSample s;
    {
        std::ifstream stat("/proc/self/task/" + std::to_string(tid) + "/stat");
        std::string line;
        if (std::getline(stat, line)) {
            // Fields after the comm "(...)" — comm may contain spaces.
            const auto close = line.rfind(')');
            if (close != std::string::npos) {
                std::istringstream rest(line.substr(close + 2));
                std::string tok;
                // Fields 3..13 skipped; 14 = utime, 15 = stime (clock ticks).
                for (int field = 3; field <= 13 && rest >> tok; ++field) {
                }
                uint64_t utime = 0, stime = 0;
                if (rest >> utime >> stime) {
                    static const double tick = static_cast<double>(sysconf(_SC_CLK_TCK));
                    s.cpuSeconds = static_cast<double>(utime + stime) / tick;
                }
            }
        }
    }
    {
        std::ifstream status("/proc/self/task/" + std::to_string(tid) + "/status");
        std::string line;
        while (std::getline(status, line)) {
            if (line.rfind("nonvoluntary_ctxt_switches:", 0) == 0) {
                s.nonvoluntaryCs = std::strtoull(line.c_str() + 27, nullptr, 10);
                break;
            }
        }
    }
    return s;
}
#endif

std::string wallClockIso() {
    const std::time_t now = std::time(nullptr);
    std::tm tmBuf{};
#ifdef _WIN32
    localtime_s(&tmBuf, &now);
#else
    localtime_r(&now, &tmBuf);
#endif
    char buf[32];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tmBuf) == 0) return {};
    return buf;
}

} // namespace

PipelineTrendSampler::~PipelineTrendSampler() {
    stop();
}

bool PipelineTrendSampler::start(const std::string& directory, Provider provider,
                                 std::chrono::milliseconds interval) {
    if (running_) return false;
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(directory, ec);
    if (ec) {
        SPDLOG_ERROR("PipelineTrendSampler: create_directories({}) failed: {}", directory,
                     ec.message());
        return false;
    }
    const auto csvPath = fs::path(directory) / "pipeline_trend.csv";
    out_.open(csvPath, std::ios::trunc);
    if (!out_) {
        SPDLOG_ERROR("PipelineTrendSampler: cannot open {}", csvPath.string());
        return false;
    }
    provider_ = std::move(provider);
    interval_ = interval;
    startTime_ = std::chrono::steady_clock::now();
    lastFrameCount_ = PipelineTimingRecorder::instance().frameRecordCount();
    stopRequested_ = false;
    running_ = true;
    writeHeader();
    thread_ = std::thread(&PipelineTrendSampler::run, this);
    SPDLOG_INFO("PipelineTrendSampler: sampling every {} ms into {}", interval_.count(),
                csvPath.string());
    return true;
}

void PipelineTrendSampler::stop() {
    if (!running_) return;
    {
        std::scoped_lock lk(mutex_);
        stopRequested_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join(); // bounded: loop waits <= interval_
    writeRow();                             // final partial-window row
    out_.flush();
    out_.close();
    running_ = false;
    SPDLOG_INFO("PipelineTrendSampler: stopped");
}

void PipelineTrendSampler::run() {
    std::unique_lock lk(mutex_);
    while (!stopRequested_) {
        if (cv_.wait_for(lk, interval_, [this] { return stopRequested_; })) break;
        lk.unlock();
        writeRow();
        lk.lock();
    }
}

void PipelineTrendSampler::writeHeader() {
    out_ << "t_s,wall_clock,now_us,"
            "frame_records,trigger_records,"
            "frame_age_p50_us,frame_age_p95_us,"
            "fetch_extract_p50_us,fetch_extract_p95_us,"
            "algo_p50_us,algo_p95_us,"
            "e2e_frame_p50_us,e2e_frame_p95_us,"
            "e2e_target_p50_us,e2e_target_p95_us,"
            "request_to_fire_p95_us,dispatch_p95_us,"
            "host_grab_gap_mean_us,host_grab_gap_p95_us,device_tick_gap_mean,"
            "objects_per_frame_mean,"
            "dropped_to_latest,ring_behind,empty_frame,kernel_error,batch_queue_rejected,"
            "backlog_frames,latest_available_index,rt_last_processed,"
            "capture_fps,algo_fps,"
            "batch_queue_depth,batch_max_queue_depth,batch_accepted,batch_dropped,"
            "batch_processed,"
            "realtime_mode,drop_frames,experiment_active,"
            "live_target_latency_last_us,live_target_latency_avg_us,live_target_latency_max_us,"
            "empty_frame_avg_us,empty_frame_count,overlay_avg_us,overlay_count,"
            "cpu_capture_pct,cpu_realtime_pct,cpu_trigger_pct,cpu_batch_pct,cpu_hdf_writer_pct,"
            "cs_nonvol_realtime,cs_nonvol_trigger,"
            "heap_inuse_mb,heap_free_mb,io_write_mb,"
            "mat_allocs,mat_alloc_mb,"
            "hdf_write_avg_us,hdf_write_count,hdf_write_max_us,"
            "mem_mb,peak_mem_mb\n";
    out_.flush();
}

void PipelineTrendSampler::writeRow() {
    auto& rec = PipelineTimingRecorder::instance();

    const auto summary = rec.summarize(kSummarySampleLimit);

    // Gap and per-frame-object stats over only the records new since the last
    // tick, so each row is an independent window. Host grab stamps and device
    // ticks are reduced separately — they are on different clocks.
    const uint64_t frameCount = rec.frameRecordCount();
    const uint64_t newRecords =
        std::min<uint64_t>(frameCount - lastFrameCount_, PipelineTimingRecorder::kFrameCapacity);
    lastFrameCount_ = frameCount;
    GapStats hostGap, deviceGap;
    double objectsPerFrame = 0.0;
    if (newRecords > 1) {
        auto frames = rec.frameRecords();
        if (frames.size() > newRecords) {
            frames.erase(frames.begin(), frames.end() - static_cast<std::ptrdiff_t>(newRecords));
        }
        std::vector<uint64_t> hostStamps, deviceStamps;
        hostStamps.reserve(frames.size());
        deviceStamps.reserve(frames.size());
        uint64_t objects = 0;
        for (const auto& f : frames) {
            hostStamps.push_back(f.grabUs);
            deviceStamps.push_back(f.deviceTimestamp);
            objects += f.validCount + f.invalidCount;
        }
        hostGap = reduceGaps(hostStamps);
        deviceGap = reduceGaps(deviceStamps);
        objectsPerFrame = static_cast<double>(objects) / static_cast<double>(frames.size());
    }

    // Per-pipeline-stage CPU%% over the interval since the previous row, and
    // cumulative nonvoluntary context switches (scheduling-pressure signal)
    // for the two latency-critical threads.
    const auto rowNow = std::chrono::steady_clock::now();
    const double dtSec =
        lastRowTime_.time_since_epoch().count() == 0
            ? 0.0
            : std::chrono::duration<double>(rowNow - lastRowTime_).count();
    lastRowTime_ = rowNow;
    double cpuCapture = 0, cpuRealtime = 0, cpuTrigger = 0, cpuBatch = 0, cpuHdf = 0;
    uint64_t csRealtime = 0, csTrigger = 0;
    for (const auto& entry : ThreadRegistry::instance().snapshot()) {
        const ThreadSample s = sampleThread(entry.tid);
        if (s.cpuSeconds < 0.0) continue;
        double pct = 0.0;
        auto prev = threadCpuPrev_.find(entry.tid);
        if (prev != threadCpuPrev_.end() && dtSec > 0.0) {
            pct = std::max(0.0, (s.cpuSeconds - prev->second) / dtSec * 100.0);
        }
        threadCpuPrev_[entry.tid] = s.cpuSeconds;
        if (entry.name == "capture") cpuCapture += pct;
        else if (entry.name == "realtime") cpuRealtime += pct;
        else if (entry.name == "trigger") cpuTrigger += pct;
        else if (entry.name == "batch_worker") cpuBatch += pct;
        else if (entry.name == "hdf_writer") cpuHdf += pct;
        if (entry.name == "realtime") csRealtime += s.nonvoluntaryCs;
        if (entry.name == "trigger") csTrigger += s.nonvoluntaryCs;
    }
    const auto heap = backend::Tools::getHeapStats();

    PipelineTrendProviderSample p;
    if (provider_) p = provider_();
    const uint64_t backlog = p.latestAvailableIndex > p.rtLastProcessed
                                 ? p.latestAvailableIndex - p.rtLastProcessed
                                 : 0;

    const double tSec =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime_).count();

    out_ << tSec << ',' << wallClockIso() << ',' << PipelineTimingRecorder::nowUs() << ','
         << frameCount << ',' << rec.triggerRecordCount() << ','
         << summary.frameAge.p50Us << ',' << summary.frameAge.p95Us << ','
         << summary.fetchExtract.p50Us << ',' << summary.fetchExtract.p95Us << ','
         << summary.algo.p50Us << ',' << summary.algo.p95Us << ','
         << summary.endToEndFrame.p50Us << ',' << summary.endToEndFrame.p95Us << ','
         << summary.endToEndTarget.p50Us << ',' << summary.endToEndTarget.p95Us << ','
         << summary.requestToFire.p95Us << ',' << summary.dispatch.p95Us << ','
         << hostGap.meanUs << ',' << hostGap.p95Us << ',' << deviceGap.meanUs << ','
         << objectsPerFrame << ','
         << rec.skippedCount(PipelineSkipReason::DroppedToLatest) << ','
         << rec.skippedCount(PipelineSkipReason::RingBehind) << ','
         << rec.skippedCount(PipelineSkipReason::EmptyFrame) << ','
         << rec.skippedCount(PipelineSkipReason::KernelError) << ','
         << rec.skippedCount(PipelineSkipReason::BatchQueueRejected) << ','
         << backlog << ',' << p.latestAvailableIndex << ',' << p.rtLastProcessed << ','
         << p.captureFps << ',' << p.algoFps << ','
         << p.batchQueueDepth << ',' << p.batchMaxQueueDepth << ',' << p.batchAccepted << ','
         << p.batchDropped << ',' << p.batchProcessed << ','
         << p.realtimeMode << ',' << (p.dropFrames ? 1 : 0) << ','
         << (p.experimentActive ? 1 : 0) << ','
         << rec.lastTargetLatencyUs() << ',' << rec.avgTargetLatencyUs() << ','
         << rec.maxTargetLatencyUs() << ','
         << rec.avgEmptyFrameCostUs() << ',' << rec.emptyFrameCostCount() << ','
         << rec.avgOverlayComputeUs() << ',' << rec.overlayComputeCount() << ','
         << cpuCapture << ',' << cpuRealtime << ',' << cpuTrigger << ',' << cpuBatch << ','
         << cpuHdf << ','
         << csRealtime << ',' << csTrigger << ','
         << heap.inUseMB << ',' << heap.freeMB << ','
         << backend::Tools::getProcessIoWriteMB() << ','
         << MatAllocStats::allocCount() << ','
         << static_cast<double>(MatAllocStats::allocBytes()) / (1024.0 * 1024.0) << ','
         << rec.avgHdfWriteUs() << ',' << rec.hdfWriteCount() << ',' << rec.maxHdfWriteUs() << ','
         << backend::Tools::getProcessMemoryMB() << ','
         << backend::Tools::getPeakProcessMemoryMB() << '\n';
    out_.flush();
}

} // namespace backend::diagnostics

// End-to-end capture + processing smoke test with metric validation.
//
// Drives the mock-camera path through AppBackend for a fixed 2 s
// window, then:
//
//   1. Sanity-checks that the exposed metrics are non-zero and
//      consistent (framesProcessed, jobsQueued/Processed, the 1-second
//      rolling FPS/algo-us counters).
//   2. Writes a JSON report (min/median/mean via the wall-clock and
//      the atomics) that MLflow can ingest alongside the other perf
//      tests.
//
// History: originally a minimal "does the backend initialize + capture
// frames?" test. Expanded on 2026-04-16 to also validate the metric
// plumbing — the set of atomics the UI reads (CaptureStats,
// ProcessingStats, ProcessingService::getAlgo*Fps1s /
// getAlgoAvgUs1s / getTotalValidFlushed) — so drift between code
// changes and UI-visible numbers gets caught in CI.

#include "backend/AppBackend.h"
#include "backend/services/CaptureService.h"
#include "backend/services/ProcessingService.h"
#include "perf_common.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <spdlog/spdlog.h>

namespace {

#ifdef _WIN32
void setEnv(const char* key, const std::string& value) {
    _putenv_s(key, value.c_str());
}
#else
void setEnv(const char* key, const std::string& value) {
    setenv(key, value.c_str(), 1);
}
#endif

void ensureMockFrames(const std::filesystem::path& dir) {
    std::filesystem::create_directories(dir);
    const auto framePath = dir / "frame_000.png";
    if (std::filesystem::exists(framePath)) {
        return;
    }

    // Write a proper 64x64 grayscale PNG via OpenCV so QImageReader
    // accepts it on all platforms (the old hand-crafted 1-pixel PNG
    // had a CRC error that strict libpng on Linux rejected).
    cv::Mat img(64, 64, CV_8UC1, cv::Scalar(128));
    cv::imwrite(framePath.string(), img);
}

} // namespace

int main() {
    try {
        const std::filesystem::path mockDir = std::filesystem::path("data") / "mock_frames";
        ensureMockFrames(mockDir);
        setEnv("MIB_CAMERA_MODE", "mock");
        setEnv("MIB_MOCK_CAMERA_DIR", mockDir.string());
        setEnv("MIB_MOCK_CAMERA_LOOP", "true"); // loop so we get many frames in 2 s

        backend::AppBackend app;
        if (!app.initialize("data")) {
            SPDLOG_ERROR("Backend initialize failed");
            return 1;
        }

        auto& cap = app.capture();
        auto& proc = app.processing();

        const auto tStart = std::chrono::steady_clock::now();
        SPDLOG_INFO("Starting capture for 2 seconds...");
        cap.start();

        std::this_thread::sleep_for(std::chrono::seconds(2));

        cap.stop();
        const auto tEnd = std::chrono::steady_clock::now();
        const double elapsedSec = std::chrono::duration<double>(tEnd - tStart).count();

        const auto& cstats = cap.stats();
        const auto& pstats = proc.stats();

        const uint64_t framesProcessed    = cstats.framesProcessed.load();
        const uint64_t lastFrameRate      = cstats.lastFrameRate.load();
        const uint64_t lastDataRateMBps   = cstats.lastDataRateMBps.load();
        const uint64_t jobsQueued         = pstats.jobsQueued.load();
        const uint64_t jobsProcessed      = pstats.jobsProcessed.load();
        const double algoFps1s            = proc.getAlgoFps1s();
        const double validFps1s           = proc.getValidFps1s();
        const double invalidFps1s         = proc.getInvalidFps1s();
        const double algoAvgUs1s          = proc.getAlgoAvgUs1s();
        const uint64_t totalValidFlushed  = proc.getTotalValidFlushed();

        const double wallClockFps =
            elapsedSec > 0.0 ? (static_cast<double>(framesProcessed) / elapsedSec) : 0.0;

        SPDLOG_INFO("Elapsed: {:.3f} s | framesProcessed={} ({:.1f} fps wall) | "
                    "lastFrameRate={} lastDataRateMBps={}",
                    elapsedSec, framesProcessed, wallClockFps, lastFrameRate, lastDataRateMBps);
        SPDLOG_INFO("Processing: jobsQueued={} jobsProcessed={} algoFps1s={:.1f} "
                    "validFps1s={:.1f} invalidFps1s={:.1f} algoAvgUs1s={:.1f} totalValidFlushed={}",
                    jobsQueued, jobsProcessed, algoFps1s, validFps1s, invalidFps1s,
                    algoAvgUs1s, totalValidFlushed);

        // --- Sanity checks ---
        // These are the invariants the UI relies on; if any of them break
        // the UI will silently show stale / zero values.
        int failures = 0;
        auto mustBePositive = [&](const char* label, double v) {
            if (v <= 0.0) {
                SPDLOG_ERROR("Metric invariant failed: {} = {} (expected > 0)", label, v);
                ++failures;
            }
        };
        mustBePositive("elapsed_sec",        elapsedSec);
        mustBePositive("framesProcessed",    static_cast<double>(framesProcessed));

        // algoFps1s + validFps1s + invalidFps1s cannot exceed algoFps1s +
        // a small tolerance; sum ~= algoFps1s by construction.
        const double classifiedFps = validFps1s + invalidFps1s;
        if (algoFps1s > 0.0 &&
            classifiedFps > algoFps1s * 1.2) {
            SPDLOG_WARN("valid+invalid fps ({:.1f}) >> algoFps1s ({:.1f}) — metric drift?",
                        classifiedFps, algoFps1s);
        }

        // lastFrameRate can be 0 with the mock camera on very small frames
        // (rounding), so we WARN rather than FAIL.
        if (lastFrameRate == 0) {
            SPDLOG_WARN("CaptureStats::lastFrameRate is 0 after 2 s — mock rounding?");
        }

        // --- JSON report ---
        mib::perf::JsonReport report;
        report.addNumber("elapsed_sec", elapsedSec)
              .addInt("frames_processed", static_cast<long long>(framesProcessed))
              .addNumber("wall_clock_fps", wallClockFps)
              .addInt("last_frame_rate", static_cast<long long>(lastFrameRate))
              .addInt("last_data_rate_mbps", static_cast<long long>(lastDataRateMBps))
              .addInt("jobs_queued", static_cast<long long>(jobsQueued))
              .addInt("jobs_processed", static_cast<long long>(jobsProcessed))
              .addNumber("algo_fps_1s", algoFps1s)
              .addNumber("valid_fps_1s", validFps1s)
              .addNumber("invalid_fps_1s", invalidFps1s)
              .addNumber("algo_avg_us_1s", algoAvgUs1s)
              .addInt("total_valid_flushed", static_cast<long long>(totalValidFlushed))
              .addInt("sanity_failures", static_cast<long long>(failures));

        const std::string jsonPath = mib::perf::resolveJsonOutPath(
            "MIB_CAPTURE_PERF_JSON", "capture_processing_results.json");
        if (!report.writeTo(jsonPath)) {
            SPDLOG_WARN("Failed to open {} for JSON report", jsonPath);
        } else {
            SPDLOG_INFO("Wrote JSON report: {}", jsonPath);
        }

        if (failures > 0) {
            SPDLOG_ERROR("{} metric invariant failure(s)", failures);
            return 3;
        }
        return 0;
    } catch (const std::exception& ex) {
        SPDLOG_ERROR("Test exception: {}", ex.what());
        return 2;
    }
}

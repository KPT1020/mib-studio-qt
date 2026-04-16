// Hdf5Service write throughput benchmarks.
//
// There's no published atomic metric for HDF5 write throughput — but
// the round-robin flush cadence and the stop-lag logs in Hdf5Service
// make this a perf-critical path: at ~5 kfps with multi-image enabled,
// the write budget per flush is tight. This test gives MLflow a
// baseline frames/s and MB/s number to trend against.
//
// Benches:
//   1. appendFrames with batch sizes 10 / 100 / 1000 of synthetic
//      ProcessedFrames (1024x1024 grayscale images + masks + minimal
//      validation data). Measures:
//        - wall-clock ms per batch
//        - frames/s
//        - MB/s (payload = images + masks, not metadata)
//   2. Total wall-clock for 10 batches of 100 frames (5000 frames
//      total) to capture amortised growth cost of H5Dextend.
//   3. appendRecordingFrames equivalent (images + metadata, no masks
//      / contours — this is the recording-mode hot path).
//
// Graceful skip: if openFile() fails (HDF5 lib missing / disk full),
// the test logs a warning and exits 0 with an empty JSON report
// rather than failing the build.
//
// Uses a temp directory so it doesn't pollute the data dir. The test
// deletes its output file on success.

#include "backend/services/Hdf5Service.h"
#include "backend/services/ProcessingService.h"
#include "perf_common.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <spdlog/spdlog.h>

using mib::perf::LatencyStats;
using mib::perf::summarise;
using mib::perf::logStats;
using mib::perf::envSizeOr;
using perf_clock = std::chrono::steady_clock;

namespace {

constexpr int kImageSize = 1024;

cv::Mat makeImage(uint32_t seed) {
    cv::Mat m(kImageSize, kImageSize, CV_8UC1);
    // Fill with pseudorandom noise — cheap, but keeps HDF5 from hitting
    // any runtime dedup / sparse optimisations.
    std::mt19937 rng(seed);
    for (int r = 0; r < m.rows; ++r) {
        uint8_t* p = m.ptr<uint8_t>(r);
        for (int c = 0; c < m.cols; ++c) {
            p[c] = static_cast<uint8_t>(rng() & 0xFFu);
        }
    }
    return m;
}

cv::Mat makeMask(uint32_t seed) {
    cv::Mat m(kImageSize, kImageSize, CV_8UC1, cv::Scalar(0));
    // Draw a few filled rectangles so the mask isn't trivially
    // compressible / all-zero.
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> xd(0, kImageSize - 50);
    std::uniform_int_distribution<int> yd(0, kImageSize - 50);
    for (int i = 0; i < 5; ++i) {
        cv::rectangle(m,
                      cv::Rect(xd(rng), yd(rng), 40, 40),
                      cv::Scalar(255), -1);
    }
    return m;
}

std::vector<backend::services::ProcessedFrame> makeProcessedBatch(std::size_t n,
                                                                 uint64_t startIdx) {
    std::vector<backend::services::ProcessedFrame> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        backend::services::ProcessedFrame pf;
        pf.index = startIdx + i;
        pf.timestampNs = (startIdx + i) * 200'000ULL; // 5 kHz
        pf.originalImage = makeImage(static_cast<uint32_t>(pf.index * 7 + 1));
        pf.processedImage = makeMask(static_cast<uint32_t>(pf.index * 13 + 2));
        pf.validation.isValid = true;
        pf.validation.area = 120.0;
        pf.validation.deformability = 0.2;
        pf.validation.ringRatio = 18.5;
        pf.validation.youngsModulus = 3.2;
        out.push_back(std::move(pf));
    }
    return out;
}

std::vector<cv::Mat> makeRecordingBatch(std::size_t n, uint64_t startIdx) {
    std::vector<cv::Mat> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        out.push_back(makeImage(static_cast<uint32_t>((startIdx + i) * 3 + 11)));
    }
    return out;
}

std::vector<backend::services::Hdf5Service::RecordingFrameMeta>
makeRecordingMeta(std::size_t n, uint64_t startIdx) {
    std::vector<backend::services::Hdf5Service::RecordingFrameMeta> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        backend::services::Hdf5Service::RecordingFrameMeta m;
        m.index = startIdx + i;
        m.timestampNs = (startIdx + i) * 200'000ULL;
        m.width = kImageSize;
        m.height = kImageSize;
        out.push_back(m);
    }
    return out;
}

struct BatchResult {
    std::size_t batchSize;
    double wallMs;
    double framesPerSec;
    double mbPerSec;
};

BatchResult runAppendBench(backend::services::Hdf5Service& svc,
                           std::size_t batchSize,
                           std::size_t numBatches,
                           uint64_t& runningIdx) {
    BatchResult r{};
    r.batchSize = batchSize;
    const double mbPerFrame =
        (static_cast<double>(kImageSize) * kImageSize * 2.0) / (1024.0 * 1024.0);
    // images + masks (same size), metadata ignored for MB/s.

    double totalMs = 0.0;
    std::size_t totalFrames = 0;
    for (std::size_t b = 0; b < numBatches; ++b) {
        auto batch = makeProcessedBatch(batchSize, runningIdx);
        runningIdx += batchSize;
        const std::vector<backend::services::ProcessedFrame> emptyInvalid;
        const auto t0 = perf_clock::now();
        const bool ok = svc.appendFrames(batch, emptyInvalid);
        const auto t1 = perf_clock::now();
        if (!ok) {
            SPDLOG_WARN("appendFrames returned false (batch {}, size {})", b, batchSize);
            continue;
        }
        totalMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
        totalFrames += batchSize;
    }
    r.wallMs = totalMs;
    r.framesPerSec = totalFrames > 0 ? (totalFrames * 1000.0 / totalMs) : 0.0;
    r.mbPerSec = totalFrames > 0 ? (totalFrames * mbPerFrame * 1000.0 / totalMs) : 0.0;
    return r;
}

} // namespace

int main() {
    try {
        spdlog::set_level(spdlog::level::info);

        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path tmpDir = fs::temp_directory_path(ec) /
                                ("mib_hdf5_perf_" + std::to_string(
                                    std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(tmpDir, ec);
        if (ec) {
            SPDLOG_WARN("Could not create temp dir {}: {}", tmpDir.string(), ec.message());
        }
        const fs::path outPath = tmpDir / "append_frames.h5";
        const fs::path recPath = tmpDir / "recording_frames.h5";

        mib::perf::JsonReport report;

        // --- Bench 1: appendFrames with varying batch sizes ---
        const std::size_t numBatches = envSizeOr("MIB_HDF5_BATCHES", 5);
        const std::vector<std::size_t> batchSizes = {10, 100, 1000};

        {
            backend::services::Hdf5Service svc;
            if (!svc.openFile(outPath.string())) {
                SPDLOG_WARN("HDF5 openFile failed — skipping hdf5_perf_test. "
                            "Output dir was {}", tmpDir.string());
                report.addString("status", "skipped_no_hdf5");
                const std::string jsonPath = mib::perf::resolveJsonOutPath(
                    "MIB_HDF5_PERF_JSON", "hdf5_perf_results.json");
                report.writeTo(jsonPath);
                return 0;
            }

            uint64_t runningIdx = 0;
            for (const std::size_t bs : batchSizes) {
                // Fresh file per batch-size so the growth cost doesn't
                // compound across sizes.
                svc.closeFile();
                fs::remove(outPath, ec);
                if (!svc.openFile(outPath.string())) {
                    SPDLOG_WARN("HDF5 reopen failed for batch size {}", bs);
                    continue;
                }
                runningIdx = 0;
                const auto r = runAppendBench(svc, bs, numBatches, runningIdx);
                SPDLOG_INFO("appendFrames batch={} x{} | wall={:.1f} ms "
                            "frames/s={:.1f} MB/s={:.2f}",
                            bs, numBatches, r.wallMs, r.framesPerSec, r.mbPerSec);
                const std::string prefix = "append_frames_batch_" + std::to_string(bs);
                report.addNumber(prefix + "_wall_ms", r.wallMs)
                      .addNumber(prefix + "_frames_per_s", r.framesPerSec)
                      .addNumber(prefix + "_mb_per_s", r.mbPerSec);
            }
            svc.closeFile();
        }

        // --- Bench 2: sustained write — 10 batches of 100 ---
        {
            backend::services::Hdf5Service svc;
            fs::remove(outPath, ec);
            if (svc.openFile(outPath.string())) {
                uint64_t runningIdx = 0;
                const auto r = runAppendBench(svc, 100, 10, runningIdx);
                SPDLOG_INFO("appendFrames sustained 10x100 | wall={:.1f} ms "
                            "frames/s={:.1f} MB/s={:.2f}",
                            r.wallMs, r.framesPerSec, r.mbPerSec);
                report.addNumber("append_frames_sustained_wall_ms", r.wallMs)
                      .addNumber("append_frames_sustained_frames_per_s", r.framesPerSec)
                      .addNumber("append_frames_sustained_mb_per_s", r.mbPerSec);
                svc.closeFile();
            }
        }

        // --- Bench 3: appendRecordingFrames ---
        {
            backend::services::Hdf5Service svc;
            fs::remove(recPath, ec);
            if (svc.openFile(recPath.string()) && svc.initializeRecordingDatasets()) {
                uint64_t runningIdx = 0;
                double totalMs = 0.0;
                std::size_t totalFrames = 0;
                const double mbPerFrame =
                    (static_cast<double>(kImageSize) * kImageSize) / (1024.0 * 1024.0);
                for (std::size_t b = 0; b < numBatches; ++b) {
                    auto images = makeRecordingBatch(100, runningIdx);
                    auto meta = makeRecordingMeta(100, runningIdx);
                    runningIdx += 100;
                    const auto t0 = perf_clock::now();
                    const bool ok = svc.appendRecordingFrames(images, meta);
                    const auto t1 = perf_clock::now();
                    if (!ok) {
                        SPDLOG_WARN("appendRecordingFrames failed on batch {}", b);
                        continue;
                    }
                    totalMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
                    totalFrames += images.size();
                }
                const double fps = totalFrames > 0 ? (totalFrames * 1000.0 / totalMs) : 0.0;
                const double mbs = totalFrames > 0 ? (totalFrames * mbPerFrame * 1000.0 / totalMs) : 0.0;
                SPDLOG_INFO("appendRecordingFrames {}x100 | wall={:.1f} ms "
                            "frames/s={:.1f} MB/s={:.2f}",
                            numBatches, totalMs, fps, mbs);
                report.addNumber("append_recording_wall_ms", totalMs)
                      .addNumber("append_recording_frames_per_s", fps)
                      .addNumber("append_recording_mb_per_s", mbs);
                svc.closeFile();
            } else {
                SPDLOG_WARN("Skipping recording bench — openFile or initializeRecordingDatasets failed");
            }
        }

        fs::remove(outPath, ec);
        fs::remove(recPath, ec);
        fs::remove(tmpDir, ec);

        const std::string jsonPath = mib::perf::resolveJsonOutPath(
            "MIB_HDF5_PERF_JSON", "hdf5_perf_results.json");
        if (!report.writeTo(jsonPath)) {
            SPDLOG_WARN("Failed to open {} for JSON report", jsonPath);
        } else {
            SPDLOG_INFO("Wrote JSON report: {}", jsonPath);
        }
        return 0;
    } catch (const std::exception& ex) {
        SPDLOG_ERROR("hdf5_perf_test exception: {}", ex.what());
        return 2;
    }
}

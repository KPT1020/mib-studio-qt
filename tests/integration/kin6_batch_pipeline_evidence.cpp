#include "backend/processing/ProcessingService.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace {

using backend::services::ProcessedFrame;
using backend::services::ProcessingConfig;
using backend::services::ProcessingService;

struct ImageRecord {
    uint64_t rowIndex{0};
    std::string path;
    int width{0};
    int height{0};
};

struct TimingSummary {
    double totalMs{0.0};
    double avgUs{0.0};
    double maxUs{0.0};
};

struct CaptureProbeMetrics {
    bool workerBlockedBeforeBurst{false};
    size_t framesAttemptedWhileBlocked{0};
    size_t framesAcceptedWhileBlocked{0};
    uint64_t framesDroppedWhileBlocked{0};
    size_t queueDepthWhileBlocked{0};
    uint64_t processedAfterRelease{0};
    TimingSummary enqueueTiming;
};

std::string jsonEscape(const std::string& value)
{
    std::ostringstream out;
    for (const char ch : value) {
        switch (ch) {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default: out << ch; break;
        }
    }
    return out.str();
}

ProcessingConfig makeEvidenceConfig()
{
    ProcessingConfig config;
    config.gaussian_blur_size = 3;
    config.bg_subtract_threshold = 180;
    config.morph_kernel_size = 3;
    config.morph_iterations = 1;
    config.enable_border_check = false;
    config.enable_area_range_check = false;
    config.enable_deformability_range_check = false;
    config.enable_ring_ratio_check = false;
    config.enable_area_ratio_check = false;
    config.require_single_inner_contour = false;
    config.empty_frame_pixel_threshold = 1;
    return config;
}

cv::Mat makeSyntheticRingFrame()
{
    cv::Mat image(96, 96, CV_8UC1, cv::Scalar(0));
    const cv::Point center(48, 48);
    cv::circle(image, center, 24, cv::Scalar(255), -1);
    cv::circle(image, center, 11, cv::Scalar(0), -1);
    return image;
}

ProcessingConfig makeSyntheticMetricConfig()
{
    ProcessingConfig config;
    config.gaussian_blur_size = 3;
    config.bg_subtract_threshold = 20;
    config.morph_kernel_size = 3;
    config.morph_iterations = 1;
    config.enable_border_check = false;
    config.enable_area_range_check = false;
    config.enable_deformability_range_check = false;
    config.enable_ring_ratio_check = false;
    config.enable_area_ratio_check = false;
    config.require_single_inner_contour = true;
    config.empty_frame_pixel_threshold = 1;
    return config;
}

std::vector<std::string> splitTab(const std::string& line)
{
    std::vector<std::string> parts;
    std::string current;
    std::istringstream in(line);
    while (std::getline(in, current, '\t')) {
        parts.push_back(current);
    }
    return parts;
}

bool readManifest(const std::filesystem::path& manifestPath, std::vector<ImageRecord>& records)
{
    std::ifstream in(manifestPath);
    if (!in) {
        std::cerr << "failed to open manifest: " << manifestPath << '\n';
        return false;
    }

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line.rfind("row_idx", 0) == 0) {
            continue;
        }

        const auto parts = splitTab(line);
        if (parts.size() < 4) {
            std::cerr << "invalid manifest line: " << line << '\n';
            return false;
        }

        ImageRecord record;
        record.rowIndex = static_cast<uint64_t>(std::stoull(parts[0]));
        record.path = parts[1];
        record.width = std::stoi(parts[2]);
        record.height = std::stoi(parts[3]);
        records.push_back(std::move(record));
    }

    if (records.empty()) {
        std::cerr << "manifest contains no image records\n";
        return false;
    }
    return true;
}

bool loadImages(const std::vector<ImageRecord>& records, std::vector<cv::Mat>& images)
{
    images.reserve(records.size());
    for (const auto& record : records) {
        cv::Mat image = cv::imread(record.path, cv::IMREAD_GRAYSCALE);
        if (image.empty()) {
            std::cerr << "failed to load image for row " << record.rowIndex << ": " << record.path << '\n';
            return false;
        }
        images.emplace_back(std::move(image));
    }
    return true;
}

TimingSummary summarizeDurations(const std::vector<double>& durationsUs)
{
    TimingSummary summary;
    if (durationsUs.empty()) {
        return summary;
    }

    double totalUs = 0.0;
    for (const double value : durationsUs) {
        totalUs += value;
        summary.maxUs = std::max(summary.maxUs, value);
    }
    summary.totalMs = totalUs / 1000.0;
    summary.avgUs = totalUs / static_cast<double>(durationsUs.size());
    return summary;
}

CaptureProbeMetrics runCaptureLoopProbe(const std::vector<cv::Mat>& images, const ProcessingConfig& processingConfig)
{
    CaptureProbeMetrics metrics;
    if (images.empty()) {
        return metrics;
    }

    ProcessingService service;
    ProcessingService::BatchPipelineConfig config;
    config.batchSize = 1;
    config.maxQueuedFrames = images.size();
    config.workerCount = 1;
    config.processing = processingConfig;

    std::mutex mutex;
    std::condition_variable condition;
    bool callbackEntered = false;
    bool releaseCallback = false;
    uint64_t emittedFrames = 0;

    service.startBatchPipeline(config, [&](std::vector<ProcessedFrame> batch) {
        std::unique_lock<std::mutex> lock(mutex);
        callbackEntered = true;
        condition.notify_all();
        condition.wait(lock, [&] { return releaseCallback; });
        emittedFrames += static_cast<uint64_t>(batch.size());
    });

    service.enqueueBatchFrame(images.front(), 0, 0);
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait_for(lock, std::chrono::seconds(10), [&] { return callbackEntered; });
    }
    metrics.workerBlockedBeforeBurst = callbackEntered;

    std::vector<double> durationsUs;
    durationsUs.reserve(images.size());
    for (size_t i = 0; i < images.size(); ++i) {
        const auto start = std::chrono::steady_clock::now();
        const bool accepted = service.enqueueBatchFrame(images[i], static_cast<uint64_t>(i + 1), static_cast<uint64_t>(i + 1));
        const auto end = std::chrono::steady_clock::now();
        durationsUs.push_back(std::chrono::duration<double, std::micro>(end - start).count());
        ++metrics.framesAttemptedWhileBlocked;
        if (accepted) {
            ++metrics.framesAcceptedWhileBlocked;
        }
    }

    const auto statsWhileBlocked = service.getBatchPipelineStats();
    metrics.framesDroppedWhileBlocked = statsWhileBlocked.framesDropped;
    metrics.queueDepthWhileBlocked = statsWhileBlocked.currentQueueDepth;
    metrics.enqueueTiming = summarizeDurations(durationsUs);

    {
        std::scoped_lock lock(mutex);
        releaseCallback = true;
    }
    condition.notify_all();
    service.stopBatchPipeline();
    metrics.processedAfterRelease = emittedFrames;
    return metrics;
}

bool runBatchEvidence(const std::vector<cv::Mat>& images,
                      const ProcessingConfig& processingConfig,
                      std::vector<ProcessedFrame>& results,
                      std::vector<size_t>& callbackBatchSizes,
                      ProcessingService::BatchPipelineStats& stats,
                      TimingSummary& enqueueTiming,
                      bool& firstCallbackAfterEnqueue)
{
    ProcessingService service;
    ProcessingService::BatchPipelineConfig config;
    config.batchSize = images.size();
    config.maxQueuedFrames = images.size();
    config.workerCount = 2;
    config.processing = processingConfig;

    std::mutex mutex;
    std::condition_variable condition;
    std::atomic<bool> enqueueComplete{false};
    bool capturedFirstCallback = false;

    const bool started = service.startBatchPipeline(config, [&](std::vector<ProcessedFrame> batch) {
        std::scoped_lock lock(mutex);
        if (!capturedFirstCallback) {
            firstCallbackAfterEnqueue = enqueueComplete.load(std::memory_order_acquire);
            capturedFirstCallback = true;
        }
        callbackBatchSizes.push_back(batch.size());
        for (auto& frame : batch) {
            results.emplace_back(std::move(frame));
        }
        condition.notify_all();
    });
    if (!started) {
        std::cerr << "failed to start batch evidence pipeline\n";
        return false;
    }

    std::vector<double> durationsUs;
    durationsUs.reserve(images.size());
    for (size_t i = 0; i < images.size(); ++i) {
        const auto start = std::chrono::steady_clock::now();
        const bool accepted = service.enqueueBatchFrame(images[i], static_cast<uint64_t>(i), static_cast<uint64_t>(i));
        const auto end = std::chrono::steady_clock::now();
        durationsUs.push_back(std::chrono::duration<double, std::micro>(end - start).count());
        if (!accepted) {
            std::cerr << "failed to enqueue image " << i << " during batch evidence run\n";
            service.stopBatchPipeline();
            return false;
        }
    }
    enqueueComplete.store(true, std::memory_order_release);
    enqueueTiming = summarizeDurations(durationsUs);

    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!condition.wait_for(lock, std::chrono::minutes(3), [&] { return results.size() == images.size(); })) {
            std::cerr << "timed out waiting for " << images.size() << " processed batch frames\n";
            service.stopBatchPipeline();
            return false;
        }
    }

    stats = service.getBatchPipelineStats();
    service.stopBatchPipeline();
    return true;
}

size_t chooseSampleIndex(const std::vector<ProcessedFrame>& results)
{
    for (size_t i = 0; i < results.size(); ++i) {
        if (!results[i].processedImage.empty() &&
            cv::countNonZero(results[i].processedImage) > 0 &&
            !results[i].validation.allContours.empty()) {
            return i;
        }
    }
    return 0;
}

bool writeVisualEvidence(const std::filesystem::path& outputDir,
                         const std::vector<ImageRecord>& records,
                         const std::vector<ProcessedFrame>& results,
                         size_t sampleIndex)
{
    if (results.empty() || sampleIndex >= results.size()) {
        std::cerr << "cannot write visual evidence without results\n";
        return false;
    }

    const auto inputPath = outputDir / "hf_input_sample.png";
    const auto maskPath = outputDir / "processed_mask_sample.png";
    const auto overlayPath = outputDir / "contour_overlay_sample.png";

    if (!cv::imwrite(inputPath.string(), results[sampleIndex].originalImage)) {
        std::cerr << "failed to write " << inputPath << '\n';
        return false;
    }
    if (!cv::imwrite(maskPath.string(), results[sampleIndex].processedImage)) {
        std::cerr << "failed to write " << maskPath << '\n';
        return false;
    }

    cv::Mat overlay;
    cv::cvtColor(results[sampleIndex].originalImage, overlay, cv::COLOR_GRAY2BGR);
    cv::drawContours(overlay, results[sampleIndex].validation.allContours, -1, cv::Scalar(0, 255, 0), 1);
    cv::putText(overlay,
                "row " + std::to_string(records[sampleIndex].rowIndex),
                cv::Point(8, 18),
                cv::FONT_HERSHEY_SIMPLEX,
                0.45,
                cv::Scalar(0, 255, 255),
                1);
    if (!cv::imwrite(overlayPath.string(), overlay)) {
        std::cerr << "failed to write " << overlayPath << '\n';
        return false;
    }

    return true;
}

bool writeMetricsJson(const std::filesystem::path& outputPath,
                      const std::vector<ImageRecord>& records,
                      const std::vector<ProcessedFrame>& results,
                      const ProcessedFrame& syntheticMetricProbe,
                      const CaptureProbeMetrics& captureProbe,
                      const ProcessingService::BatchPipelineStats& stats,
                      const TimingSummary& batchEnqueueTiming,
                      const std::vector<size_t>& callbackBatchSizes,
                      bool firstCallbackAfterEnqueue,
                      size_t sampleIndex)
{
    std::ofstream out(outputPath);
    if (!out) {
        std::cerr << "failed to open metrics output: " << outputPath << '\n';
        return false;
    }

    const size_t maxCallbackBatch = callbackBatchSizes.empty()
                                        ? 0
                                        : *std::max_element(callbackBatchSizes.begin(), callbackBatchSizes.end());
    size_t nonEmptyMasks = 0;
    size_t contourFrames = 0;
    size_t nonZeroRingWidthFrames = 0;
    for (const auto& frame : results) {
        if (!frame.processedImage.empty() && cv::countNonZero(frame.processedImage) > 0) {
            ++nonEmptyMasks;
        }
        if (!frame.validation.allContours.empty()) {
            ++contourFrames;
        }
        if (frame.validation.ringRatio > 0.0) {
            ++nonZeroRingWidthFrames;
        }
    }

    out << std::fixed << std::setprecision(6);
    out << "{\n";
    out << "  \"dataset\": {\n";
    out << "    \"repo\": \"gavinlouuu/512x96stream\",\n";
    out << "    \"config\": \"default\",\n";
    out << "    \"split\": \"train\",\n";
    out << "    \"rows_loaded\": " << records.size() << "\n";
    out << "  },\n";
    out << "  \"capture_loop_probe\": {\n";
    out << "    \"worker_blocked_before_enqueue_burst\": " << (captureProbe.workerBlockedBeforeBurst ? "true" : "false") << ",\n";
    out << "    \"frames_attempted_while_worker_blocked\": " << captureProbe.framesAttemptedWhileBlocked << ",\n";
    out << "    \"frames_accepted_while_worker_blocked\": " << captureProbe.framesAcceptedWhileBlocked << ",\n";
    out << "    \"frames_dropped_while_worker_blocked\": " << captureProbe.framesDroppedWhileBlocked << ",\n";
    out << "    \"queue_depth_while_worker_blocked\": " << captureProbe.queueDepthWhileBlocked << ",\n";
    out << "    \"processed_after_release\": " << captureProbe.processedAfterRelease << ",\n";
    out << "    \"enqueue_total_ms\": " << captureProbe.enqueueTiming.totalMs << ",\n";
    out << "    \"enqueue_avg_us\": " << captureProbe.enqueueTiming.avgUs << ",\n";
    out << "    \"enqueue_max_us\": " << captureProbe.enqueueTiming.maxUs << "\n";
    out << "  },\n";
    out << "  \"batch_run\": {\n";
    out << "    \"batch_size\": " << stats.batchSize << ",\n";
    out << "    \"worker_count\": " << stats.workerCount << ",\n";
    out << "    \"accepted_frames\": " << stats.framesAccepted << ",\n";
    out << "    \"frames_processed\": " << stats.framesProcessed << ",\n";
    out << "    \"frames_dropped\": " << stats.framesDropped << ",\n";
    out << "    \"batches_processed\": " << stats.batchesProcessed << ",\n";
    out << "    \"max_queue_depth\": " << stats.maxQueueDepth << ",\n";
    out << "    \"callback_batch_sizes\": [";
    for (size_t i = 0; i < callbackBatchSizes.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << callbackBatchSizes[i];
    }
    out << "],\n";
    out << "    \"max_callback_batch_size\": " << maxCallbackBatch << ",\n";
    out << "    \"first_callback_after_enqueue_loop\": " << (firstCallbackAfterEnqueue ? "true" : "false") << ",\n";
    out << "    \"enqueue_total_ms\": " << batchEnqueueTiming.totalMs << ",\n";
    out << "    \"enqueue_avg_us\": " << batchEnqueueTiming.avgUs << ",\n";
    out << "    \"enqueue_max_us\": " << batchEnqueueTiming.maxUs << "\n";
    out << "  },\n";
    out << "  \"metric_summary\": {\n";
    out << "    \"non_empty_masks\": " << nonEmptyMasks << ",\n";
    out << "    \"frames_with_contours\": " << contourFrames << ",\n";
    out << "    \"frames_with_nonzero_ring_width\": " << nonZeroRingWidthFrames << "\n";
    out << "  },\n";
    out << "  \"sample\": {\n";
    out << "    \"sample_index\": " << sampleIndex << ",\n";
    out << "    \"row_index\": " << records[sampleIndex].rowIndex << ",\n";
    out << "    \"source_path\": \"" << jsonEscape(records[sampleIndex].path) << "\",\n";
    out << "    \"area\": " << results[sampleIndex].validation.area << ",\n";
    out << "    \"deformability\": " << results[sampleIndex].validation.deformability << ",\n";
    out << "    \"ring_width\": " << results[sampleIndex].validation.ringRatio << ",\n";
    out << "    \"ring_ratio\": " << results[sampleIndex].validation.ringRatio << ",\n";
    out << "    \"inner_contour_count\": " << results[sampleIndex].validation.innerContourCount << "\n";
    out << "  },\n";
    out << "  \"synthetic_metric_probe\": {\n";
    out << "    \"source\": \"generated nested-contour ring frame\",\n";
    out << "    \"area\": " << syntheticMetricProbe.validation.area << ",\n";
    out << "    \"deformability\": " << syntheticMetricProbe.validation.deformability << ",\n";
    out << "    \"ring_width\": " << syntheticMetricProbe.validation.ringRatio << ",\n";
    out << "    \"ring_ratio\": " << syntheticMetricProbe.validation.ringRatio << ",\n";
    out << "    \"inner_contour_count\": " << syntheticMetricProbe.validation.innerContourCount << ",\n";
    out << "    \"is_valid\": " << (syntheticMetricProbe.validation.isValid ? "true" : "false") << "\n";
    out << "  },\n";
    out << "  \"per_frame_metrics\": [\n";
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& validation = results[i].validation;
        out << "    {";
        out << "\"row_index\": " << records[i].rowIndex << ", ";
        out << "\"area\": " << validation.area << ", ";
        out << "\"deformability\": " << validation.deformability << ", ";
        out << "\"ring_width\": " << validation.ringRatio << ", ";
        out << "\"ring_ratio\": " << validation.ringRatio << ", ";
        out << "\"is_valid\": " << (validation.isValid ? "true" : "false") << ", ";
        out << "\"contour_count\": " << validation.allContours.size();
        out << "}";
        if (i + 1 < results.size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ]\n";
    out << "}\n";
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: " << argv[0] << " <output-dir> <hf-image-manifest.tsv>\n";
        return 2;
    }

    const std::filesystem::path outputDir = argv[1];
    const std::filesystem::path manifestPath = argv[2];
    std::filesystem::create_directories(outputDir);

    std::vector<ImageRecord> records;
    if (!readManifest(manifestPath, records)) {
        return 3;
    }
    if (records.size() != 5000) {
        std::cerr << "expected 5000 HF rows, got " << records.size() << '\n';
        return 4;
    }

    std::vector<cv::Mat> images;
    if (!loadImages(records, images)) {
        return 5;
    }

    const ProcessingConfig processingConfig = makeEvidenceConfig();
    const CaptureProbeMetrics captureProbe = runCaptureLoopProbe(images, processingConfig);
    ProcessingService metricProbeService;
    const ProcessedFrame syntheticMetricProbe = metricProbeService.computeProcessedFrame(
        makeSyntheticRingFrame(),
        cv::Mat{},
        makeSyntheticMetricConfig(),
        ProcessingService::Roi{0, 0, 0, 0},
        0,
        0);

    std::vector<ProcessedFrame> results;
    std::vector<size_t> callbackBatchSizes;
    ProcessingService::BatchPipelineStats stats;
    TimingSummary batchEnqueueTiming;
    bool firstCallbackAfterEnqueue = false;
    if (!runBatchEvidence(images,
                          processingConfig,
                          results,
                          callbackBatchSizes,
                          stats,
                          batchEnqueueTiming,
                          firstCallbackAfterEnqueue)) {
        return 6;
    }

    if (results.size() != records.size()) {
        std::cerr << "processed result count mismatch: " << results.size() << " vs " << records.size() << '\n';
        return 7;
    }

    const size_t sampleIndex = chooseSampleIndex(results);
    if (!writeVisualEvidence(outputDir, records, results, sampleIndex)) {
        return 8;
    }

    if (!writeMetricsJson(outputDir / "metrics.json",
                          records,
                          results,
                          syntheticMetricProbe,
                          captureProbe,
                          stats,
                          batchEnqueueTiming,
                          callbackBatchSizes,
                          firstCallbackAfterEnqueue,
                          sampleIndex)) {
        return 9;
    }

    std::cout << "KIN-6 evidence generated at " << outputDir << '\n';
    std::cout << "accepted_frames=" << stats.framesAccepted
              << " frames_processed=" << stats.framesProcessed
              << " frames_dropped=" << stats.framesDropped
              << " max_callback_batch_size="
              << (callbackBatchSizes.empty() ? 0 : *std::max_element(callbackBatchSizes.begin(), callbackBatchSizes.end()))
              << " capture_probe_accepted_while_blocked=" << captureProbe.framesAcceptedWhileBlocked
              << '\n';
    return 0;
}

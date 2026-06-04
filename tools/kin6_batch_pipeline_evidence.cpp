#include "backend/services/ProcessingService.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace {

using backend::services::ProcessedFrame;
using backend::services::ProcessingService;

struct FrameMetric {
    uint64_t index{0};
    uint64_t timestampNs{0};
    int width{0};
    int height{0};
    int maskNonZeroPixels{0};
    bool isValid{false};
    double areaPx2{0.0};
    double deformability{0.0};
    double ringWidthPx{0.0};
    double areaRatio{0.0};
    int innerContourCount{0};
    double brightnessQ1{0.0};
    double brightnessQ2{0.0};
    double brightnessQ3{0.0};
    double brightnessQ4{0.0};
};

struct MetricSummary {
    size_t frameCount{0};
    size_t validCount{0};
    size_t framesWithInnerContours{0};
    double areaMin{std::numeric_limits<double>::max()};
    double areaMax{0.0};
    double areaSum{0.0};
    double deformabilityMin{std::numeric_limits<double>::max()};
    double deformabilityMax{0.0};
    double deformabilitySum{0.0};
    double ringWidthMin{std::numeric_limits<double>::max()};
    double ringWidthMax{0.0};
    double ringWidthSum{0.0};
    int maskNonZeroMin{std::numeric_limits<int>::max()};
    int maskNonZeroMax{0};
    uint64_t firstIndex{0};
    uint64_t lastIndex{0};

    void add(const FrameMetric& metric)
    {
        if (frameCount == 0) {
            firstIndex = metric.index;
        }
        lastIndex = metric.index;
        ++frameCount;
        if (metric.isValid) {
            ++validCount;
        }
        if (metric.innerContourCount > 0) {
            ++framesWithInnerContours;
        }
        areaMin = std::min(areaMin, metric.areaPx2);
        areaMax = std::max(areaMax, metric.areaPx2);
        areaSum += metric.areaPx2;
        deformabilityMin = std::min(deformabilityMin, metric.deformability);
        deformabilityMax = std::max(deformabilityMax, metric.deformability);
        deformabilitySum += metric.deformability;
        ringWidthMin = std::min(ringWidthMin, metric.ringWidthPx);
        ringWidthMax = std::max(ringWidthMax, metric.ringWidthPx);
        ringWidthSum += metric.ringWidthPx;
        maskNonZeroMin = std::min(maskNonZeroMin, metric.maskNonZeroPixels);
        maskNonZeroMax = std::max(maskNonZeroMax, metric.maskNonZeroPixels);
    }

    double areaAverage() const { return frameCount == 0 ? 0.0 : areaSum / static_cast<double>(frameCount); }
    double deformabilityAverage() const { return frameCount == 0 ? 0.0 : deformabilitySum / static_cast<double>(frameCount); }
    double ringWidthAverage() const { return frameCount == 0 ? 0.0 : ringWidthSum / static_cast<double>(frameCount); }
    double safeAreaMin() const { return frameCount == 0 ? 0.0 : areaMin; }
    double safeDeformabilityMin() const { return frameCount == 0 ? 0.0 : deformabilityMin; }
    double safeRingWidthMin() const { return frameCount == 0 ? 0.0 : ringWidthMin; }
    int safeMaskNonZeroMin() const { return frameCount == 0 ? 0 : maskNonZeroMin; }
};

struct SampleFrame {
    bool present{false};
    uint64_t index{0};
    double score{0.0};
    FrameMetric metric;
    cv::Mat original;
    cv::Mat mask;
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
};

bool waitForResults(std::condition_variable& cv,
                    std::mutex& mutex,
                    const std::function<bool()>& predicate)
{
    std::unique_lock<std::mutex> lk(mutex);
    return cv.wait_for(lk, std::chrono::minutes(3), predicate);
}

std::string boolText(bool value)
{
    return value ? "true" : "false";
}

std::string jsonString(const std::string& value)
{
    std::ostringstream out;
    out << '"';
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
    out << '"';
    return out.str();
}

std::string baseName(const std::string& path)
{
    return std::filesystem::path(path).filename().string();
}

FrameMetric makeMetric(const ProcessedFrame& frame)
{
    FrameMetric metric;
    metric.index = frame.index;
    metric.timestampNs = frame.timestampNs;
    metric.width = frame.originalImage.cols;
    metric.height = frame.originalImage.rows;
    metric.maskNonZeroPixels = frame.processedImage.empty() ? 0 : cv::countNonZero(frame.processedImage);
    metric.isValid = frame.validation.isValid;
    metric.areaPx2 = frame.validation.area;
    metric.deformability = frame.validation.deformability;
    metric.ringWidthPx = frame.validation.ringRatio;
    metric.areaRatio = frame.validation.areaRatio;
    metric.innerContourCount = frame.validation.innerContourCount;
    metric.brightnessQ1 = frame.validation.brightness.q1;
    metric.brightnessQ2 = frame.validation.brightness.q2;
    metric.brightnessQ3 = frame.validation.brightness.q3;
    metric.brightnessQ4 = frame.validation.brightness.q4;
    return metric;
}

double sampleScore(const FrameMetric& metric)
{
    return (static_cast<double>(metric.innerContourCount) * 1'000'000.0) +
           (metric.ringWidthPx * 10'000.0) +
           (metric.isValid ? 1000.0 : 0.0) +
           metric.areaPx2 +
           static_cast<double>(metric.maskNonZeroPixels);
}

void considerSample(SampleFrame& sample, const ProcessedFrame& frame, const FrameMetric& metric)
{
    const double score = sampleScore(metric);
    if (sample.present && score <= sample.score) {
        return;
    }
    sample.present = true;
    sample.index = frame.index;
    sample.score = score;
    sample.metric = metric;
    sample.original = frame.originalImage.clone();
    sample.mask = frame.processedImage.clone();
    sample.contours = frame.validation.allContours;
    sample.hierarchy = frame.validation.hierarchy;
}

bool writeContourOverlay(const SampleFrame& sample, const std::string& path)
{
    if (!sample.present || sample.original.empty() || sample.mask.empty()) {
        return false;
    }

    cv::Mat base;
    cv::cvtColor(sample.original, base, cv::COLOR_GRAY2BGR);

    cv::Mat colorMask(base.size(), base.type(), cv::Scalar(0, 0, 160));
    cv::Mat overlay = base.clone();
    colorMask.copyTo(overlay, sample.mask);
    cv::addWeighted(overlay, 0.35, base, 0.65, 0.0, overlay);

    for (size_t i = 0; i < sample.contours.size(); ++i) {
        const bool inner = i < sample.hierarchy.size() && sample.hierarchy[i][3] >= 0;
        const cv::Scalar color = inner ? cv::Scalar(0, 220, 255) : cv::Scalar(0, 255, 80);
        cv::drawContours(overlay,
                         sample.contours,
                         static_cast<int>(i),
                         color,
                         2,
                         cv::LINE_AA,
                         sample.hierarchy);
    }

    return cv::imwrite(path, overlay);
}

void writeFrameMetrics(std::ofstream& out,
                       const std::vector<FrameMetric>& frameMetrics,
                       const std::vector<std::string>& inputPaths)
{
    out << "  \"frames\": [\n";
    for (size_t i = 0; i < frameMetrics.size(); ++i) {
        const FrameMetric& frame = frameMetrics[i];
        if (i > 0) {
            out << ",\n";
        }
        const std::string inputFile = frame.index < inputPaths.size()
                                          ? baseName(inputPaths[static_cast<size_t>(frame.index)])
                                          : "";
        out << "    {\n";
        out << "      \"index\": " << frame.index << ",\n";
        out << "      \"timestamp_ns\": " << frame.timestampNs << ",\n";
        out << "      \"input_file\": " << jsonString(inputFile) << ",\n";
        out << "      \"width\": " << frame.width << ",\n";
        out << "      \"height\": " << frame.height << ",\n";
        out << "      \"mask_nonzero_pixels\": " << frame.maskNonZeroPixels << ",\n";
        out << "      \"is_valid\": " << boolText(frame.isValid) << ",\n";
        out << "      \"area_px2\": " << frame.areaPx2 << ",\n";
        out << "      \"deformability\": " << frame.deformability << ",\n";
        out << "      \"ring_width_px\": " << frame.ringWidthPx << ",\n";
        out << "      \"area_ratio\": " << frame.areaRatio << ",\n";
        out << "      \"inner_contour_count\": " << frame.innerContourCount << ",\n";
        out << "      \"brightness\": {\n";
        out << "        \"q1\": " << frame.brightnessQ1 << ",\n";
        out << "        \"q2\": " << frame.brightnessQ2 << ",\n";
        out << "        \"q3\": " << frame.brightnessQ3 << ",\n";
        out << "        \"q4\": " << frame.brightnessQ4 << "\n";
        out << "      }\n";
        out << "    }";
    }
    out << "\n  ],\n";
}

void writeMetricsJson(const std::string& path,
                      const ProcessingService::BatchPipelineConfig& config,
                      const ProcessingService::BatchPipelineStats& stats,
                      const std::vector<size_t>& callbackBatchSizes,
                      const std::vector<FrameMetric>& frameMetrics,
                      const MetricSummary& summary,
                      const SampleFrame& sample,
                      const std::vector<std::string>& inputPaths,
                      size_t requestedFrames,
                      size_t acceptedFrames,
                      size_t rejectedFrames,
                      long long enqueueElapsedUs)
{
    const size_t maxCallbackBatch = callbackBatchSizes.empty()
                                        ? 0
                                        : *std::max_element(callbackBatchSizes.begin(), callbackBatchSizes.end());

    std::ofstream out(path);
    out << std::fixed << std::setprecision(6);
    out << "{\n";
    out << "  \"dataset\": {\n";
    out << "    \"name\": \"gavinlouuu/512x96stream\",\n";
    out << "    \"config\": \"default\",\n";
    out << "    \"split\": \"train\",\n";
    out << "    \"source\": \"Hugging Face Dataset Viewer rows API\",\n";
    out << "    \"requested_count\": " << requestedFrames << ",\n";
    out << "    \"input_count\": " << inputPaths.size() << ",\n";
    out << "    \"background_file\": " << jsonString(inputPaths.empty() ? "" : baseName(inputPaths.front())) << "\n";
    out << "  },\n";
    out << "  \"pipeline\": {\n";
    out << "    \"batch_size\": " << config.batchSize << ",\n";
    out << "    \"worker_count\": " << config.workerCount << ",\n";
    out << "    \"max_queued_frames\": " << config.maxQueuedFrames << ",\n";
    out << "    \"max_batch_wait_ms\": " << config.maxBatchWaitMs << ",\n";
    out << "    \"max_callback_batch_size\": " << maxCallbackBatch << ",\n";
    out << "    \"batch_size_at_least_5000\": " << boolText(config.batchSize >= 5000) << ",\n";
    out << "    \"emitted_batch_at_least_5000\": " << boolText(maxCallbackBatch >= 5000) << "\n";
    out << "  },\n";
    out << "  \"enqueue\": {\n";
    out << "    \"accepted_frames\": " << acceptedFrames << ",\n";
    out << "    \"rejected_frames\": " << rejectedFrames << ",\n";
    out << "    \"elapsed_us\": " << enqueueElapsedUs << "\n";
    out << "  },\n";
    out << "  \"stats\": {\n";
    out << "    \"frames_enqueued\": " << stats.framesEnqueued << ",\n";
    out << "    \"frames_dropped\": " << stats.framesDropped << ",\n";
    out << "    \"frames_processed\": " << stats.framesProcessed << ",\n";
    out << "    \"batches_processed\": " << stats.batchesProcessed << ",\n";
    out << "    \"queued_frames\": " << stats.queuedFrames << ",\n";
    out << "    \"running\": " << boolText(stats.running) << "\n";
    out << "  },\n";
    out << "  \"callback_batch_sizes\": [";
    for (size_t i = 0; i < callbackBatchSizes.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << callbackBatchSizes[i];
    }
    out << "],\n";
    out << "  \"summary\": {\n";
    out << "    \"frame_count\": " << summary.frameCount << ",\n";
    out << "    \"valid_count\": " << summary.validCount << ",\n";
    out << "    \"frames_with_inner_contours\": " << summary.framesWithInnerContours << ",\n";
    out << "    \"first_index\": " << summary.firstIndex << ",\n";
    out << "    \"last_index\": " << summary.lastIndex << ",\n";
    out << "    \"area_px2\": {\"min\": " << summary.safeAreaMin() << ", \"max\": " << summary.areaMax << ", \"avg\": " << summary.areaAverage() << "},\n";
    out << "    \"deformability\": {\"min\": " << summary.safeDeformabilityMin() << ", \"max\": " << summary.deformabilityMax << ", \"avg\": " << summary.deformabilityAverage() << "},\n";
    out << "    \"ring_width_px\": {\"min\": " << summary.safeRingWidthMin() << ", \"max\": " << summary.ringWidthMax << ", \"avg\": " << summary.ringWidthAverage() << "},\n";
    out << "    \"mask_nonzero_pixels\": {\"min\": " << summary.safeMaskNonZeroMin() << ", \"max\": " << summary.maskNonZeroMax << "}\n";
    out << "  },\n";
    out << "  \"sample_result\": {\n";
    out << "    \"index\": " << sample.metric.index << ",\n";
    out << "    \"input_file\": " << jsonString(sample.metric.index < inputPaths.size() ? baseName(inputPaths[static_cast<size_t>(sample.metric.index)]) : "") << ",\n";
    out << "    \"area_px2\": " << sample.metric.areaPx2 << ",\n";
    out << "    \"deformability\": " << sample.metric.deformability << ",\n";
    out << "    \"ring_width_px\": " << sample.metric.ringWidthPx << ",\n";
    out << "    \"mask_nonzero_pixels\": " << sample.metric.maskNonZeroPixels << "\n";
    out << "  },\n";
    writeFrameMetrics(out, frameMetrics, inputPaths);
    out << "  \"artifacts\": {\n";
    out << "    \"sample_input\": \"hf_input_sample.png\",\n";
    out << "    \"sample_processed_mask\": \"processed_mask_sample.png\",\n";
    out << "    \"sample_contour_overlay\": \"contour_overlay_sample.png\",\n";
    out << "    \"dataset_rows\": \"hf_rows.json\",\n";
    out << "    \"dataset_size\": \"hf_size.json\",\n";
    out << "    \"dataset_manifest\": \"hf_image_downloads.tsv\"\n";
    out << "  }\n";
    out << "}\n";
}

void writeReadme(const std::string& path,
                 const ProcessingService::BatchPipelineConfig& config,
                 const ProcessingService::BatchPipelineStats& stats,
                 const std::vector<size_t>& callbackBatchSizes,
                 const SampleFrame& sample)
{
    const size_t maxCallbackBatch = callbackBatchSizes.empty()
                                        ? 0
                                        : *std::max_element(callbackBatchSizes.begin(), callbackBatchSizes.end());

    std::ofstream out(path);
    out << "# KIN-6 Batch Pipeline Evidence\n\n";
    out << "This bundle verifies the async batch pipeline against Hugging Face dataset ";
    out << "`gavinlouuu/512x96stream` (`default/train`).\n\n";
    out << "## Regenerate\n\n";
    out << "```bash\n";
    out << "cmake --preset linux-backend-only\n";
    out << "cmake --build --preset linux-backend-only-build --target kin6_batch_pipeline_evidence\n";
    out << "tools/kin6_generate_hf_evidence.sh review_artifacts/KIN-6 build/linux-backend/kin6_batch_pipeline_evidence\n";
    out << "```\n\n";
    out << "## High-volume gate\n\n";
    out << "- Configured batch size: " << config.batchSize << "\n";
    out << "- Worker count: " << config.workerCount << "\n";
    out << "- Max queued frames: " << config.maxQueuedFrames << "\n";
    out << "- Accepted frames: " << stats.framesEnqueued << "\n";
    out << "- Processed frames: " << stats.framesProcessed << "\n";
    out << "- Dropped frames: " << stats.framesDropped << "\n";
    out << "- Max emitted callback batch size: " << maxCallbackBatch << "\n\n";
    out << "## Outputs\n\n";
    out << "- `hf_input_sample.png` - dataset frame submitted to `enqueueBatchFrame()`.\n";
    out << "- `processed_mask_sample.png` - mask emitted by async batch workers.\n";
    out << "- `contour_overlay_sample.png` - input plus mask/contours for inspection.\n";
    out << "- `metrics.json` - queue stats, callback batch sizes, and area/deformability/ring-width metrics.\n";
    out << "- `hf_rows.json`, `hf_rows_preview.json`, `hf_size.json`, `hf_splits.json`, `hf_is_valid.json` - Dataset Viewer API evidence.\n";
    out << "- `hf_image_downloads.tsv` - row-to-image manifest used by the generator.\n\n";
    out << "The first downloaded row is used as `BatchPipelineConfig::backgroundGray` ";
    out << "so the mask reflects the same background-subtract stage documented for migration.\n\n";
    out << "Sample frame index: " << sample.index << ".\n";
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::cerr << "usage: " << argv[0] << " <output-directory> <hf-image> [hf-image...]\n";
        return 1;
    }

    const std::filesystem::path outputDir = argv[1];
    std::filesystem::create_directories(outputDir);

    std::vector<std::string> inputPaths;
    inputPaths.reserve(static_cast<size_t>(argc - 2));
    for (int i = 2; i < argc; ++i) {
        inputPaths.emplace_back(argv[i]);
    }

    cv::Mat background = cv::imread(inputPaths.front(), cv::IMREAD_GRAYSCALE);
    if (background.empty()) {
        std::cerr << "failed to load Hugging Face background image: " << inputPaths.front() << "\n";
        return 2;
    }

    ProcessingService service;

    ProcessingService::BatchPipelineConfig config;
    config.batchSize = std::max<size_t>(5000, inputPaths.size());
    config.workerCount = 2;
    config.maxQueuedFrames = config.batchSize;
    config.maxBatchWaitMs = inputPaths.size() >= 5000 ? 120000 : 1000;
    config.processingConfig.enable_area_range_check = false;
    config.processingConfig.enable_ring_ratio_check = false;
    config.processingConfig.enable_deformability_range_check = false;
    config.processingConfig.enable_border_check = false;
    config.processingConfig.require_single_inner_contour = false;
    config.backgroundGray = background;

    std::mutex mutex;
    std::condition_variable cv;
    std::vector<FrameMetric> frameMetrics;
    std::vector<size_t> callbackBatchSizes;
    MetricSummary summary;
    SampleFrame sample;
    size_t processedFrames = 0;

    frameMetrics.reserve(inputPaths.size());
    service.setBatchResultCallback([&](std::vector<ProcessedFrame>&& batch) {
        std::lock_guard<std::mutex> lk(mutex);
        callbackBatchSizes.push_back(batch.size());
        for (const ProcessedFrame& frame : batch) {
            FrameMetric metric = makeMetric(frame);
            summary.add(metric);
            considerSample(sample, frame, metric);
            frameMetrics.push_back(metric);
            ++processedFrames;
        }
        cv.notify_all();
    });

    service.startBatchPipeline(config);

    size_t acceptedFrames = 0;
    size_t rejectedFrames = 0;
    const auto enqueueStart = std::chrono::steady_clock::now();
    for (size_t i = 0; i < inputPaths.size(); ++i) {
        cv::Mat image = cv::imread(inputPaths[i], cv::IMREAD_GRAYSCALE);
        if (image.empty()) {
            service.stopBatchPipeline();
            std::cerr << "failed to load Hugging Face image: " << inputPaths[i] << "\n";
            return 3;
        }
        const bool accepted = service.enqueueBatchFrame(image.data,
                                                        static_cast<size_t>(image.step) * static_cast<size_t>(image.rows),
                                                        static_cast<uint64_t>(image.cols),
                                                        static_cast<uint64_t>(image.rows),
                                                        static_cast<size_t>(image.step),
                                                        0,
                                                        1'000'000 + i,
                                                        i);
        if (accepted) {
            ++acceptedFrames;
        } else {
            ++rejectedFrames;
        }
    }
    const auto enqueueElapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - enqueueStart).count();

    const bool completed = waitForResults(cv, mutex, [&] {
        return processedFrames >= acceptedFrames;
    });

    service.stopBatchPipeline();

    if (!completed) {
        std::cerr << "timed out waiting for async batch results\n";
        return 4;
    }
    if (!sample.present) {
        std::cerr << "async batch pipeline emitted no frames\n";
        return 5;
    }

    std::sort(frameMetrics.begin(), frameMetrics.end(), [](const FrameMetric& lhs, const FrameMetric& rhs) {
        return lhs.index < rhs.index;
    });

    const auto stats = service.getBatchPipelineStats();
    const size_t maxCallbackBatch = callbackBatchSizes.empty()
                                        ? 0
                                        : *std::max_element(callbackBatchSizes.begin(), callbackBatchSizes.end());
    if (inputPaths.size() >= 5000 && maxCallbackBatch < 5000) {
        std::cerr << "expected an emitted batch of at least 5000 frames; max callback batch was "
                  << maxCallbackBatch << "\n";
        return 6;
    }
    if (stats.framesDropped != 0 || stats.framesProcessed != acceptedFrames) {
        std::cerr << "unexpected async stats: processed=" << stats.framesProcessed
                  << " accepted=" << acceptedFrames
                  << " dropped=" << stats.framesDropped << "\n";
        return 7;
    }

    const std::string inputPath = (outputDir / "hf_input_sample.png").string();
    const std::string maskPath = (outputDir / "processed_mask_sample.png").string();
    const std::string overlayPath = (outputDir / "contour_overlay_sample.png").string();
    const std::string metricsPath = (outputDir / "metrics.json").string();
    const std::string readmePath = (outputDir / "README.md").string();

    if (!cv::imwrite(inputPath, sample.original)) {
        std::cerr << "failed to write " << inputPath << "\n";
        return 8;
    }
    if (!cv::imwrite(maskPath, sample.mask)) {
        std::cerr << "failed to write " << maskPath << "\n";
        return 9;
    }
    if (!writeContourOverlay(sample, overlayPath)) {
        std::cerr << "failed to write " << overlayPath << "\n";
        return 10;
    }

    writeMetricsJson(metricsPath,
                     config,
                     stats,
                     callbackBatchSizes,
                     frameMetrics,
                     summary,
                     sample,
                     inputPaths,
                     inputPaths.size(),
                     acceptedFrames,
                     rejectedFrames,
                     enqueueElapsedUs);
    writeReadme(readmePath, config, stats, callbackBatchSizes, sample);

    std::cout << "KIN-6 Hugging Face async batch evidence generated at " << outputDir << "\n";
    std::cout << "dataset=gavinlouuu/512x96stream split=default/train inputs=" << inputPaths.size() << "\n";
    std::cout << "batch_size=" << config.batchSize
              << " max_callback_batch=" << maxCallbackBatch
              << " workers=" << config.workerCount << "\n";
    std::cout << "accepted=" << acceptedFrames
              << " processed=" << stats.framesProcessed
              << " dropped=" << stats.framesDropped
              << " batches=" << stats.batchesProcessed
              << " enqueue_us=" << enqueueElapsedUs << "\n";
    std::cout << std::fixed << std::setprecision(3)
              << "sample_index=" << sample.index
              << " area_px2=" << sample.metric.areaPx2
              << " deformability=" << sample.metric.deformability
              << " ring_width_px=" << sample.metric.ringWidthPx << "\n";

    return 0;
}

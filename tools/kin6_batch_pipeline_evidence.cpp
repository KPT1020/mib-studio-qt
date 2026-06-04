#include "backend/services/ProcessingService.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
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

struct InputFrame {
    std::string path;
    cv::Mat image;
};

bool waitForResults(std::condition_variable& cv,
                    std::mutex& mutex,
                    const std::function<bool()>& predicate)
{
    std::unique_lock<std::mutex> lk(mutex);
    return cv.wait_for(lk, std::chrono::seconds(10), predicate);
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

double sampleScore(const ProcessedFrame& frame)
{
    const double maskPixels = frame.processedImage.empty()
                                  ? 0.0
                                  : static_cast<double>(cv::countNonZero(frame.processedImage));
    return (static_cast<double>(frame.validation.innerContourCount) * 1'000'000.0) +
           (frame.validation.ringRatio * 10'000.0) +
           frame.validation.area +
           maskPixels;
}

bool writeContourOverlay(const ProcessedFrame& frame, const std::string& path)
{
    if (frame.originalImage.empty() || frame.processedImage.empty()) {
        return false;
    }

    cv::Mat base;
    cv::cvtColor(frame.originalImage, base, cv::COLOR_GRAY2BGR);

    cv::Mat colorMask(base.size(), base.type(), cv::Scalar(0, 0, 160));
    cv::Mat overlay = base.clone();
    colorMask.copyTo(overlay, frame.processedImage);
    cv::addWeighted(overlay, 0.35, base, 0.65, 0.0, overlay);

    for (size_t i = 0; i < frame.validation.allContours.size(); ++i) {
        const bool inner = i < frame.validation.hierarchy.size() &&
                           frame.validation.hierarchy[i][3] >= 0;
        const cv::Scalar color = inner ? cv::Scalar(0, 220, 255) : cv::Scalar(0, 255, 80);
        cv::drawContours(overlay,
                         frame.validation.allContours,
                         static_cast<int>(i),
                         color,
                         2,
                         cv::LINE_AA,
                         frame.validation.hierarchy);
    }

    return cv::imwrite(path, overlay);
}

void writeMetricsJson(const std::string& path,
                      const ProcessingService::BatchPipelineConfig& config,
                      const ProcessingService::BatchPipelineStats& stats,
                      const std::vector<size_t>& callbackBatchSizes,
                      const std::vector<ProcessedFrame>& results,
                      const std::vector<std::string>& inputPaths,
                      size_t samplePosition,
                      int acceptedFrames,
                      int rejectedFrames,
                      long long enqueueElapsedUs)
{
    std::ofstream out(path);
    out << std::fixed << std::setprecision(6);
    out << "{\n";
    out << "  \"dataset\": {\n";
    out << "    \"name\": \"gavinlouuu/512x96stream\",\n";
    out << "    \"config\": \"default\",\n";
    out << "    \"split\": \"train\",\n";
    out << "    \"source\": \"Hugging Face Dataset Viewer rows API\",\n";
    out << "    \"input_count\": " << inputPaths.size() << ",\n";
    out << "    \"background_file\": " << jsonString(inputPaths.empty() ? "" : baseName(inputPaths.front())) << "\n";
    out << "  },\n";
    out << "  \"pipeline\": {\n";
    out << "    \"batch_size\": " << config.batchSize << ",\n";
    out << "    \"worker_count\": " << config.workerCount << ",\n";
    out << "    \"max_queued_frames\": " << config.maxQueuedFrames << "\n";
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
        if (i > 0) out << ", ";
        out << callbackBatchSizes[i];
    }
    out << "],\n";
    out << "  \"sample_result_index\": " << results[samplePosition].index << ",\n";
    out << "  \"frames\": [\n";
    for (size_t i = 0; i < results.size(); ++i) {
        const ProcessedFrame& frame = results[i];
        if (i > 0) out << ",\n";
        out << "    {\n";
        out << "      \"index\": " << frame.index << ",\n";
        out << "      \"timestamp_ns\": " << frame.timestampNs << ",\n";
        out << "      \"input_file\": " << jsonString(i < inputPaths.size() ? baseName(inputPaths[i]) : "") << ",\n";
        out << "      \"width\": " << frame.originalImage.cols << ",\n";
        out << "      \"height\": " << frame.originalImage.rows << ",\n";
        out << "      \"mask_nonzero_pixels\": " << cv::countNonZero(frame.processedImage) << ",\n";
        out << "      \"is_valid\": " << boolText(frame.validation.isValid) << ",\n";
        out << "      \"area_px2\": " << frame.validation.area << ",\n";
        out << "      \"deformability\": " << frame.validation.deformability << ",\n";
        out << "      \"ring_width_px\": " << frame.validation.ringRatio << ",\n";
        out << "      \"area_ratio\": " << frame.validation.areaRatio << ",\n";
        out << "      \"inner_contour_count\": " << frame.validation.innerContourCount << ",\n";
        out << "      \"brightness\": {\n";
        out << "        \"q1\": " << frame.validation.brightness.q1 << ",\n";
        out << "        \"q2\": " << frame.validation.brightness.q2 << ",\n";
        out << "        \"q3\": " << frame.validation.brightness.q3 << ",\n";
        out << "        \"q4\": " << frame.validation.brightness.q4 << "\n";
        out << "      }\n";
        out << "    }";
    }
    out << "\n  ],\n";
    out << "  \"artifacts\": {\n";
    out << "    \"sample_input\": \"hf_input_sample.png\",\n";
    out << "    \"sample_processed_mask\": \"processed_mask_sample.png\",\n";
    out << "    \"sample_contour_overlay\": \"contour_overlay_sample.png\",\n";
    out << "    \"dataset_rows\": \"hf_rows.json\",\n";
    out << "    \"dataset_size\": \"hf_size.json\"\n";
    out << "  }\n";
    out << "}\n";
}

void writeReadme(const std::string& path,
                 const std::vector<std::string>& inputPaths,
                 const ProcessingService::BatchPipelineStats& stats)
{
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
    out << "## Inputs\n\n";
    for (const std::string& inputPath : inputPaths) {
        out << "- `" << baseName(inputPath) << "`\n";
    }
    out << "\n## Outputs\n\n";
    out << "- `hf_input_sample.png` - dataset frame submitted to `enqueueBatchFrame()`.\n";
    out << "- `processed_mask_sample.png` - mask emitted by async batch workers.\n";
    out << "- `contour_overlay_sample.png` - input plus mask/contours for inspection.\n";
    out << "- `metrics.json` - queue stats and area/deformability/ring-width metrics.\n";
    out << "- `hf_rows.json`, `hf_size.json`, `hf_splits.json`, `hf_is_valid.json` - Dataset Viewer API evidence.\n\n";
    out << "The first downloaded row is used as `BatchPipelineConfig::backgroundGray` ";
    out << "so the mask reflects the same background-subtract stage documented for migration.\n\n";
    out << "Processed frames: " << stats.framesProcessed << ", dropped frames: " << stats.framesDropped;
    out << ", emitted batches: " << stats.batchesProcessed << ".\n";
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

    std::vector<InputFrame> inputs;
    inputs.reserve(static_cast<size_t>(argc - 2));
    for (int i = 2; i < argc; ++i) {
        cv::Mat image = cv::imread(argv[i], cv::IMREAD_GRAYSCALE);
        if (image.empty()) {
            std::cerr << "failed to load Hugging Face image: " << argv[i] << "\n";
            return 2;
        }
        inputs.push_back(InputFrame{argv[i], std::move(image)});
    }

    ProcessingService service;

    ProcessingService::BatchPipelineConfig config;
    config.batchSize = 3;
    config.workerCount = 2;
    config.maxQueuedFrames = 16;
    config.processingConfig.enable_area_range_check = false;
    config.processingConfig.enable_ring_ratio_check = false;
    config.processingConfig.enable_deformability_range_check = false;
    config.processingConfig.enable_border_check = false;
    config.processingConfig.require_single_inner_contour = false;
    config.backgroundGray = inputs.front().image;

    std::mutex mutex;
    std::condition_variable cv;
    std::vector<ProcessedFrame> results;
    std::vector<size_t> callbackBatchSizes;

    service.setBatchResultCallback([&](std::vector<ProcessedFrame>&& batch) {
        std::lock_guard<std::mutex> lk(mutex);
        callbackBatchSizes.push_back(batch.size());
        for (auto& frame : batch) {
            results.emplace_back(std::move(frame));
        }
        cv.notify_all();
    });

    service.startBatchPipeline(config);

    int acceptedFrames = 0;
    int rejectedFrames = 0;
    const auto enqueueStart = std::chrono::steady_clock::now();
    for (size_t i = 0; i < inputs.size(); ++i) {
        const cv::Mat& image = inputs[i].image;
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
        return results.size() >= static_cast<size_t>(acceptedFrames);
    });

    service.stopBatchPipeline();

    if (!completed) {
        std::cerr << "timed out waiting for async batch results\n";
        return 3;
    }
    if (results.empty()) {
        std::cerr << "async batch pipeline emitted no frames\n";
        return 4;
    }

    std::sort(results.begin(), results.end(), [](const ProcessedFrame& lhs, const ProcessedFrame& rhs) {
        return lhs.index < rhs.index;
    });

    const auto sampleIt = std::max_element(results.begin(), results.end(), [](const ProcessedFrame& lhs,
                                                                              const ProcessedFrame& rhs) {
        return sampleScore(lhs) < sampleScore(rhs);
    });
    const size_t samplePosition = static_cast<size_t>(std::distance(results.begin(), sampleIt));
    const ProcessedFrame& sample = results[samplePosition];

    const std::string inputPath = (outputDir / "hf_input_sample.png").string();
    const std::string maskPath = (outputDir / "processed_mask_sample.png").string();
    const std::string overlayPath = (outputDir / "contour_overlay_sample.png").string();
    const std::string metricsPath = (outputDir / "metrics.json").string();
    const std::string readmePath = (outputDir / "README.md").string();

    if (!cv::imwrite(inputPath, sample.originalImage)) {
        std::cerr << "failed to write " << inputPath << "\n";
        return 5;
    }
    if (!cv::imwrite(maskPath, sample.processedImage)) {
        std::cerr << "failed to write " << maskPath << "\n";
        return 6;
    }
    if (!writeContourOverlay(sample, overlayPath)) {
        std::cerr << "failed to write " << overlayPath << "\n";
        return 7;
    }

    std::vector<std::string> inputPaths;
    inputPaths.reserve(inputs.size());
    for (const InputFrame& input : inputs) {
        inputPaths.push_back(input.path);
    }

    const auto stats = service.getBatchPipelineStats();
    writeMetricsJson(metricsPath,
                     config,
                     stats,
                     callbackBatchSizes,
                     results,
                     inputPaths,
                     samplePosition,
                     acceptedFrames,
                     rejectedFrames,
                     enqueueElapsedUs);
    writeReadme(readmePath, inputPaths, stats);

    std::cout << "KIN-6 Hugging Face async batch evidence generated at " << outputDir << "\n";
    std::cout << "dataset=gavinlouuu/512x96stream split=default/train inputs=" << inputs.size() << "\n";
    std::cout << "accepted=" << acceptedFrames
              << " processed=" << stats.framesProcessed
              << " dropped=" << stats.framesDropped
              << " batches=" << stats.batchesProcessed
              << " enqueue_us=" << enqueueElapsedUs << "\n";
    std::cout << std::fixed << std::setprecision(3)
              << "sample_index=" << sample.index
              << " area_px2=" << sample.validation.area
              << " deformability=" << sample.validation.deformability
              << " ring_width_px=" << sample.validation.ringRatio << "\n";

    return 0;
}

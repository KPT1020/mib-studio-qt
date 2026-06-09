#include "backend/processing/ProcessingService.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <set>
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
    std::string caseType;
};

struct SampleMetrics {
    std::string sampleId;
    uint64_t rowIndex{0};
    std::string caseType;
    std::string sourcePath;
    std::string inputPath;
    std::string maskPath;
    std::string overlayPath;
    int width{0};
    int height{0};
    int maskPixels{0};
    size_t contourCount{0};
    double area{0.0};
    double deformability{0.0};
    double ringRatio{0.0};
    int innerContourCount{0};
    bool isValid{false};
    size_t outputRecordCount{0};
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

std::string sampleIdFor(uint64_t rowIndex)
{
    std::ostringstream out;
    out << "hf-row-" << std::setw(5) << std::setfill('0') << rowIndex;
    return out.str();
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
        record.caseType = parts.size() >= 5 ? parts[4] : "hf-stream-sample";
        records.push_back(std::move(record));
    }

    if (records.size() < 3) {
        std::cerr << "expected at least 3 HF image records, got " << records.size() << '\n';
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
        if (image.cols != record.width || image.rows != record.height) {
            std::cerr << "row " << record.rowIndex << " dimension mismatch: manifest "
                      << record.width << 'x' << record.height << ", loaded "
                      << image.cols << 'x' << image.rows << '\n';
            return false;
        }
        images.emplace_back(std::move(image));
    }
    return true;
}

ProcessingConfig makeHfDatasetConfig()
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

bool runBatchPipeline(const std::vector<cv::Mat>& images,
                      std::vector<ProcessedFrame>& results,
                      ProcessingService::BatchPipelineStats& stats,
                      std::vector<size_t>& callbackBatchSizes)
{
    ProcessingService service;
    ProcessingService::BatchPipelineConfig config;
    config.batchSize = images.size();
    config.maxQueuedFrames = images.size();
    config.workerCount = 2;
    config.processing = makeHfDatasetConfig();

    std::mutex mutex;
    std::condition_variable condition;
    std::set<uint64_t> completedFrameIndices;

    const bool started = service.startBatchPipeline(config, [&](std::vector<ProcessedFrame> batch) {
        std::scoped_lock lock(mutex);
        callbackBatchSizes.push_back(batch.size());
        for (auto& frame : batch) {
            completedFrameIndices.insert(frame.index);
            results.emplace_back(std::move(frame));
        }
        condition.notify_all();
    });
    if (!started) {
        std::cerr << "failed to start HF dataset batch pipeline\n";
        return false;
    }

    for (size_t i = 0; i < images.size(); ++i) {
        if (!service.enqueueBatchFrame(images[i], static_cast<uint64_t>(i), static_cast<uint64_t>(i))) {
            std::cerr << "failed to enqueue HF dataset image " << i << '\n';
            service.stopBatchPipeline();
            return false;
        }
    }

    {
        std::unique_lock lock(mutex);
        const bool finished = condition.wait_for(lock, std::chrono::seconds(60), [&] {
            return completedFrameIndices.size() == images.size();
        });
        if (!finished) {
            std::cerr << "timed out waiting for " << images.size()
                      << " HF dataset frames, got " << completedFrameIndices.size()
                      << " completed source frames from " << results.size()
                      << " output records\n";
            service.stopBatchPipeline();
            return false;
        }
    }

    stats = service.getBatchPipelineStats();
    service.stopBatchPipeline();
    return true;
}

bool writeSampleArtifacts(const std::filesystem::path& outputDir,
                          const std::vector<ImageRecord>& records,
                          const std::vector<ProcessedFrame>& results,
                          std::vector<SampleMetrics>& samples)
{
    const auto samplesDir = outputDir / "samples";
    std::filesystem::create_directories(samplesDir);

    std::vector<std::vector<size_t>> resultIndicesByRecord(records.size());
    for (size_t i = 0; i < results.size(); ++i) {
        const auto recordIndex = results[i].index;
        if (recordIndex >= records.size()) {
            std::cerr << "result index " << recordIndex
                      << " outside manifest record count " << records.size() << '\n';
            return false;
        }
        resultIndicesByRecord[static_cast<size_t>(recordIndex)].push_back(i);
    }

    for (size_t i = 0; i < records.size(); ++i) {
        const auto& record = records[i];
        const auto& candidates = resultIndicesByRecord[i];
        if (candidates.empty()) {
            std::cerr << "row " << record.rowIndex << " produced no output records\n";
            return false;
        }

        size_t selected = candidates.front();
        for (const auto candidate : candidates) {
            const auto& frame = results[candidate];
            const bool hasMask = !frame.processedImage.empty() && cv::countNonZero(frame.processedImage) > 0;
            const bool hasContour = !frame.validation.allContours.empty();
            if (frame.validation.isValid && hasMask && hasContour) {
                selected = candidate;
                break;
            }
            if (selected == candidates.front() && hasMask && hasContour) {
                selected = candidate;
            }
        }

        const auto& frame = results[selected];
        if (frame.originalImage.empty() || frame.processedImage.empty()) {
            std::cerr << "row " << record.rowIndex << " produced an empty image or mask\n";
            return false;
        }

        SampleMetrics sample;
        sample.sampleId = sampleIdFor(record.rowIndex);
        sample.rowIndex = record.rowIndex;
        sample.caseType = record.caseType;
        sample.sourcePath = record.path;
        sample.width = frame.originalImage.cols;
        sample.height = frame.originalImage.rows;
        sample.maskPixels = cv::countNonZero(frame.processedImage);
        sample.contourCount = frame.validation.allContours.size();
        sample.area = frame.validation.area;
        sample.deformability = frame.validation.deformability;
        sample.ringRatio = frame.validation.ringRatio;
        sample.innerContourCount = frame.validation.innerContourCount;
        sample.isValid = frame.validation.isValid;
        sample.outputRecordCount = candidates.size();

        const auto inputPath = samplesDir / (sample.sampleId + "-input.png");
        const auto maskPath = samplesDir / (sample.sampleId + "-mask.png");
        const auto overlayPath = samplesDir / (sample.sampleId + "-overlay.png");
        if (!cv::imwrite(inputPath.string(), frame.originalImage)) {
            std::cerr << "failed to write " << inputPath << '\n';
            return false;
        }
        if (!cv::imwrite(maskPath.string(), frame.processedImage)) {
            std::cerr << "failed to write " << maskPath << '\n';
            return false;
        }

        cv::Mat overlay;
        cv::cvtColor(frame.originalImage, overlay, cv::COLOR_GRAY2BGR);
        cv::drawContours(overlay, frame.validation.allContours, -1, cv::Scalar(0, 255, 0), 1);
        cv::putText(overlay,
                    sample.sampleId,
                    cv::Point(8, 18),
                    cv::FONT_HERSHEY_SIMPLEX,
                    0.45,
                    cv::Scalar(0, 255, 255),
                    1);
        cv::putText(overlay,
                    "mask=" + std::to_string(sample.maskPixels) +
                        " contours=" + std::to_string(sample.contourCount),
                    cv::Point(8, 36),
                    cv::FONT_HERSHEY_SIMPLEX,
                    0.40,
                    cv::Scalar(0, 255, 255),
                    1);
        if (!cv::imwrite(overlayPath.string(), overlay)) {
            std::cerr << "failed to write " << overlayPath << '\n';
            return false;
        }

        sample.inputPath = inputPath.string();
        sample.maskPath = maskPath.string();
        sample.overlayPath = overlayPath.string();
        samples.push_back(std::move(sample));
    }

    return true;
}

void writeMetricsJson(const std::filesystem::path& outputPath,
                      const std::vector<ImageRecord>& records,
                      const ProcessingService::BatchPipelineStats& stats,
                      const std::vector<size_t>& callbackBatchSizes,
                      size_t outputRecordCount,
                      const std::vector<SampleMetrics>& samples,
                      const std::vector<std::string>& failures)
{
    std::ofstream out(outputPath);
    if (!out) {
        std::cerr << "failed to write metrics JSON: " << outputPath << '\n';
        return;
    }

    size_t nonEmptyMasks = 0;
    size_t contourFrames = 0;
    for (const auto& sample : samples) {
        if (sample.maskPixels > 0) {
            ++nonEmptyMasks;
        }
        if (sample.contourCount > 0) {
            ++contourFrames;
        }
    }

    const size_t maxCallbackBatch = callbackBatchSizes.empty()
                                        ? 0
                                        : *std::max_element(callbackBatchSizes.begin(), callbackBatchSizes.end());

    out << std::fixed << std::setprecision(6);
    out << "{\n";
    out << "  \"dataset\": {\n";
    out << "    \"repo\": \"gavinlouuu/512x96stream\",\n";
    out << "    \"config\": \"default\",\n";
    out << "    \"split\": \"train\",\n";
    out << "    \"sample_count\": " << records.size() << ",\n";
    out << "    \"row_indices\": [";
    for (size_t i = 0; i < records.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << records[i].rowIndex;
    }
    out << "]\n";
    out << "  },\n";
    out << "  \"batch_pipeline\": {\n";
    out << "    \"batch_size\": " << stats.batchSize << ",\n";
    out << "    \"worker_count\": " << stats.workerCount << ",\n";
    out << "    \"output_record_count\": " << outputRecordCount << ",\n";
    out << "    \"frames_accepted\": " << stats.framesAccepted << ",\n";
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
    out << "    \"max_callback_batch_size\": " << maxCallbackBatch << "\n";
    out << "  },\n";
    out << "  \"aggregate\": {\n";
    out << "    \"non_empty_masks\": " << nonEmptyMasks << ",\n";
    out << "    \"frames_with_contours\": " << contourFrames << "\n";
    out << "  },\n";
    out << "  \"samples\": [\n";
    for (size_t i = 0; i < samples.size(); ++i) {
        const auto& sample = samples[i];
        out << "    {\n";
        out << "      \"sample_id\": \"" << jsonEscape(sample.sampleId) << "\",\n";
        out << "      \"row_index\": " << sample.rowIndex << ",\n";
        out << "      \"case_type\": \"" << jsonEscape(sample.caseType) << "\",\n";
        out << "      \"source_path\": \"" << jsonEscape(sample.sourcePath) << "\",\n";
        out << "      \"input_path\": \"" << jsonEscape(sample.inputPath) << "\",\n";
        out << "      \"mask_path\": \"" << jsonEscape(sample.maskPath) << "\",\n";
        out << "      \"overlay_path\": \"" << jsonEscape(sample.overlayPath) << "\",\n";
        out << "      \"width\": " << sample.width << ",\n";
        out << "      \"height\": " << sample.height << ",\n";
        out << "      \"mask_pixels\": " << sample.maskPixels << ",\n";
        out << "      \"contour_count\": " << sample.contourCount << ",\n";
        out << "      \"area\": " << sample.area << ",\n";
        out << "      \"deformability\": " << sample.deformability << ",\n";
        out << "      \"ring_ratio\": " << sample.ringRatio << ",\n";
        out << "      \"inner_contour_count\": " << sample.innerContourCount << ",\n";
        out << "      \"output_record_count\": " << sample.outputRecordCount << ",\n";
        out << "      \"is_valid\": " << (sample.isValid ? "true" : "false") << "\n";
        out << "    }";
        if (i + 1 < samples.size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ],\n";
    out << "  \"failures\": [";
    for (size_t i = 0; i < failures.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << '"' << jsonEscape(failures[i]) << '"';
    }
    out << "]\n";
    out << "}\n";
}

void collectFailures(const std::vector<ImageRecord>& records,
                     const ProcessingService::BatchPipelineStats& stats,
                     const std::vector<SampleMetrics>& samples,
                     std::vector<std::string>& failures)
{
    const auto sampleCount = static_cast<uint64_t>(records.size());
    if (stats.framesAccepted != sampleCount) {
        failures.push_back("expected frames_accepted=" + std::to_string(sampleCount) +
                           ", got " + std::to_string(stats.framesAccepted));
    }
    if (stats.framesProcessed != sampleCount) {
        failures.push_back("expected frames_processed=" + std::to_string(sampleCount) +
                           ", got " + std::to_string(stats.framesProcessed));
    }
    if (stats.framesDropped != 0) {
        failures.push_back("expected frames_dropped=0, got " + std::to_string(stats.framesDropped));
    }
    if (stats.batchesProcessed == 0) {
        failures.push_back("expected at least one processed batch");
    }

    size_t nonEmptyMasks = 0;
    size_t contourFrames = 0;
    for (const auto& sample : samples) {
        if (sample.width != 512 || sample.height != 96) {
            failures.push_back(sample.sampleId + " expected 512x96 image, got " +
                               std::to_string(sample.width) + "x" + std::to_string(sample.height));
        }
        if (sample.maskPixels > 0) {
            ++nonEmptyMasks;
        }
        if (sample.maskPixels <= 0) {
            failures.push_back(sample.sampleId + " produced an empty processed mask");
        }
        if (sample.contourCount > 0) {
            ++contourFrames;
        }
        if (sample.contourCount == 0) {
            failures.push_back(sample.sampleId + " produced no contours");
        }
        if (!std::isfinite(sample.area) || sample.area <= 0.0 || sample.area > 512.0 * 96.0) {
            failures.push_back(sample.sampleId + " area is outside expected image-space range: " +
                               std::to_string(sample.area));
        }
        if (!std::isfinite(sample.deformability) || sample.deformability < 0.0 || sample.deformability > 1.0) {
            failures.push_back(sample.sampleId + " deformability is outside [0, 1]: " +
                               std::to_string(sample.deformability));
        }
        if (!sample.isValid) {
            failures.push_back(sample.sampleId + " was marked invalid by the regression config");
        }
    }

    if (nonEmptyMasks < 3) {
        failures.push_back("expected at least 3 non-empty masks, got " + std::to_string(nonEmptyMasks));
    }
    if (contourFrames < 3) {
        failures.push_back("expected at least 3 contour-bearing frames, got " + std::to_string(contourFrames));
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: " << argv[0] << " <output-dir> <hf-sample-manifest.tsv>\n";
        return 2;
    }

    const std::filesystem::path outputDir = argv[1];
    const std::filesystem::path manifestPath = argv[2];
    std::filesystem::create_directories(outputDir);

    std::vector<ImageRecord> records;
    if (!readManifest(manifestPath, records)) {
        return 3;
    }

    std::vector<cv::Mat> images;
    if (!loadImages(records, images)) {
        return 4;
    }

    std::vector<ProcessedFrame> results;
    std::vector<size_t> callbackBatchSizes;
    ProcessingService::BatchPipelineStats stats;
    if (!runBatchPipeline(images, results, stats, callbackBatchSizes)) {
        return 5;
    }
    if (results.size() < records.size()) {
        std::cerr << "result count mismatch: expected at least " << records.size()
                  << ", got " << results.size() << '\n';
        return 6;
    }

    std::vector<SampleMetrics> samples;
    if (!writeSampleArtifacts(outputDir, records, results, samples)) {
        return 7;
    }

    std::vector<std::string> failures;
    collectFailures(records, stats, samples, failures);
    writeMetricsJson(outputDir / "metrics.json", records, stats, callbackBatchSizes, results.size(), samples, failures);

    if (!failures.empty()) {
        std::cerr << "HF dataset pipeline regression detected:\n";
        for (const auto& failure : failures) {
            std::cerr << " - " << failure << '\n';
        }
        std::cerr << "Metrics and sample artifacts written under " << outputDir << '\n';
        return 1;
    }

    std::cout << "KIN-10 HF dataset pipeline test passed: samples=" << samples.size()
              << " accepted=" << stats.framesAccepted
              << " processed=" << stats.framesProcessed
              << " dropped=" << stats.framesDropped
              << " metrics=" << (outputDir / "metrics.json") << '\n';
    return 0;
}

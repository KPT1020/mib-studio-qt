#include "backend/services/ProcessingService.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <fstream>
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

cv::Mat makeRingFrame(const cv::Point& center)
{
    cv::Mat frame(128, 128, CV_8UC1, cv::Scalar(0));
    cv::ellipse(frame, center, cv::Size(31, 25), 8.0, 0.0, 360.0, cv::Scalar(240), cv::FILLED);
    cv::ellipse(frame, center, cv::Size(14, 11), 8.0, 0.0, 360.0, cv::Scalar(0), cv::FILLED);
    return frame;
}

bool waitForResults(std::condition_variable& cv,
                    std::mutex& mutex,
                    const std::function<bool()>& predicate)
{
    std::unique_lock<std::mutex> lk(mutex);
    return cv.wait_for(lk, std::chrono::seconds(5), predicate);
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
        const bool inner = i < frame.validation.hierarchy.size() && frame.validation.hierarchy[i][3] >= 0;
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

std::string boolText(bool value)
{
    return value ? "true" : "false";
}

void writeMetricsJson(const std::string& path,
                      const ProcessingService::BatchPipelineConfig& config,
                      const ProcessingService::BatchPipelineStats& stats,
                      const std::vector<size_t>& callbackBatchSizes,
                      const ProcessedFrame& sample,
                      int acceptedFrames,
                      int rejectedFrames,
                      long long enqueueElapsedUs)
{
    std::ofstream out(path);
    out << std::fixed << std::setprecision(6);
    out << "{\n";
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
    out << "  \"sample_frame\": {\n";
    out << "    \"index\": " << sample.index << ",\n";
    out << "    \"timestamp_ns\": " << sample.timestampNs << ",\n";
    out << "    \"is_valid\": " << boolText(sample.validation.isValid) << ",\n";
    out << "    \"area_px2\": " << sample.validation.area << ",\n";
    out << "    \"deformability\": " << sample.validation.deformability << ",\n";
    out << "    \"ring_width_px\": " << sample.validation.ringRatio << ",\n";
    out << "    \"area_ratio\": " << sample.validation.areaRatio << ",\n";
    out << "    \"inner_contour_count\": " << sample.validation.innerContourCount << ",\n";
    out << "    \"brightness\": {\n";
    out << "      \"q1\": " << sample.validation.brightness.q1 << ",\n";
    out << "      \"q2\": " << sample.validation.brightness.q2 << ",\n";
    out << "      \"q3\": " << sample.validation.brightness.q3 << ",\n";
    out << "      \"q4\": " << sample.validation.brightness.q4 << "\n";
    out << "    }\n";
    out << "  },\n";
    out << "  \"artifacts\": {\n";
    out << "    \"input\": \"input_ring.png\",\n";
    out << "    \"processed_mask\": \"processed_mask.png\",\n";
    out << "    \"contour_overlay\": \"contour_overlay.png\"\n";
    out << "  }\n";
    out << "}\n";
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " <output-directory>\n";
        return 1;
    }

    const std::string outputDir = argv[1];
    const std::string inputPath = outputDir + "/input_ring.png";
    const std::string maskPath = outputDir + "/processed_mask.png";
    const std::string overlayPath = outputDir + "/contour_overlay.png";
    const std::string metricsPath = outputDir + "/metrics.json";

    ProcessingService service;

    ProcessingService::BatchPipelineConfig config;
    config.batchSize = 2;
    config.workerCount = 2;
    config.maxQueuedFrames = 8;
    config.processingConfig.enable_area_range_check = false;
    config.processingConfig.enable_ring_ratio_check = false;
    config.processingConfig.enable_deformability_range_check = false;

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

    const std::vector<cv::Mat> frames = {
        makeRingFrame(cv::Point(64, 64)),
        makeRingFrame(cv::Point(61, 66)),
        makeRingFrame(cv::Point(67, 62)),
    };

    service.startBatchPipeline(config);

    int acceptedFrames = 0;
    int rejectedFrames = 0;
    const auto enqueueStart = std::chrono::steady_clock::now();
    for (size_t i = 0; i < frames.size(); ++i) {
        const cv::Mat& frame = frames[i];
        const bool accepted = service.enqueueBatchFrame(frame.data,
                                                        frame.total(),
                                                        static_cast<uint64_t>(frame.cols),
                                                        static_cast<uint64_t>(frame.rows),
                                                        static_cast<size_t>(frame.step),
                                                        0,
                                                        1'000'000 + i,
                                                        100 + i);
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
        return 2;
    }
    if (results.empty()) {
        std::cerr << "async batch pipeline emitted no frames\n";
        return 3;
    }

    std::sort(results.begin(), results.end(), [](const ProcessedFrame& lhs, const ProcessedFrame& rhs) {
        return lhs.index < rhs.index;
    });

    const ProcessedFrame& sample = results.front();
    if (!cv::imwrite(inputPath, sample.originalImage)) {
        std::cerr << "failed to write " << inputPath << "\n";
        return 4;
    }
    if (!cv::imwrite(maskPath, sample.processedImage)) {
        std::cerr << "failed to write " << maskPath << "\n";
        return 5;
    }
    if (!writeContourOverlay(sample, overlayPath)) {
        std::cerr << "failed to write " << overlayPath << "\n";
        return 6;
    }

    const auto stats = service.getBatchPipelineStats();
    writeMetricsJson(metricsPath,
                     config,
                     stats,
                     callbackBatchSizes,
                     sample,
                     acceptedFrames,
                     rejectedFrames,
                     enqueueElapsedUs);

    std::cout << "KIN-6 async batch evidence generated at " << outputDir << "\n";
    std::cout << "accepted=" << acceptedFrames
              << " processed=" << stats.framesProcessed
              << " dropped=" << stats.framesDropped
              << " batches=" << stats.batchesProcessed
              << " enqueue_us=" << enqueueElapsedUs << "\n";
    std::cout << std::fixed << std::setprecision(3)
              << "sample area_px2=" << sample.validation.area
              << " deformability=" << sample.validation.deformability
              << " ring_width_px=" << sample.validation.ringRatio << "\n";

    return 0;
}

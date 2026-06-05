#include "backend/services/ProcessingService.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

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

struct FrameMetrics {
    std::string sampleId;
    uint64_t rowIndex{0};
    std::string caseType;
    std::string sourcePath;
    std::string prediction;
    int width{0};
    int height{0};
    int maskPixels{0};
    size_t contourCount{0};
    int innerContourCount{0};
    bool emptyDiscarded{false};
    bool isValid{false};
    int foregroundPixels{0};
    int morphPixels{0};
    double roiOccupancy{0.0};
    double diffMean{0.0};
    double thresholdStableRatio{0.0};
    int backgroundShiftX{0};
    int backgroundShiftY{0};
    double backgroundAlignmentImprovement{0.0};
};

struct ReviewSample {
    FrameMetrics metrics;
    std::string inputPath;
    std::string maskPath;
    std::string overlayPath;
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

std::string sampleIdForRow(uint64_t rowIndex)
{
    std::ostringstream out;
    out << "hf-row-" << std::setw(5) << std::setfill('0') << rowIndex;
    return out.str();
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
        record.caseType = parts.size() >= 5 ? parts[4] : "hf-stream-row";
        records.push_back(std::move(record));
    }
    return !records.empty();
}

bool loadImages(const std::vector<ImageRecord>& records, std::vector<cv::Mat>& images)
{
    images.reserve(records.size());
    for (const auto& record : records) {
        cv::Mat image = cv::imread(record.path, cv::IMREAD_GRAYSCALE);
        if (image.empty()) {
            std::cerr << "failed to load image for row " << record.rowIndex << ": "
                      << record.path << '\n';
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

ProcessingConfig makeKin7Config()
{
    ProcessingConfig config;
    config.gaussian_blur_size = 3;
    config.bg_subtract_threshold = 8;
    config.morph_kernel_size = 3;
    config.morph_iterations = 1;
    config.enable_border_check = false;
    config.enable_area_range_check = false;
    config.enable_deformability_range_check = false;
    config.enable_ring_ratio_check = false;
    config.enable_area_ratio_check = false;
    config.require_single_inner_contour = true;
    config.empty_frame_pixel_threshold = 100;
    config.enable_empty_frame_precheck = true;
    config.empty_frame_min_morph_pixels = 160;
    config.empty_frame_min_roi_occupancy = 0.003;
    config.empty_frame_min_diff_mean = 0.35;
    config.empty_frame_threshold_sensitivity_step = 16;
    config.empty_frame_min_threshold_stable_ratio = 0.25;
    config.background_alignment_max_shift_px = 3;
    config.background_alignment_min_improvement = 0.25;
    return config;
}

cv::Mat makeEdgeBackground(int edgeX, int width = 512, int height = 96)
{
    cv::Mat image(height, width, CV_8UC1, cv::Scalar(18));
    edgeX = std::max(0, std::min(edgeX, width));
    if (edgeX < width) {
        image(cv::Rect(edgeX, 0, width - edgeX, height)).setTo(cv::Scalar(118));
    }
    return image;
}

cv::Mat makeSparseNoiseOnBackground(const cv::Mat& background)
{
    cv::Mat image = background.clone();
    int written = 0;
    for (int y = 8; y < image.rows && written < 220; y += 7) {
        for (int x = 12; x < image.cols - 20 && written < 220; x += 23) {
            image.at<uchar>(y, x) = 72;
            ++written;
        }
    }
    return image;
}

cv::Mat makeRingOnBackground(const cv::Mat& background)
{
    cv::Mat image = background.clone();
    const cv::Point center(256, 48);
    cv::circle(image, center, 24, cv::Scalar(255), cv::FILLED);
    cv::circle(image, center, 11, cv::Scalar(18), cv::FILLED);
    return image;
}

cv::Mat buildMeanBackground(const std::vector<cv::Mat>& images)
{
    if (images.empty()) {
        return {};
    }
    cv::Mat accum(images.front().rows, images.front().cols, CV_32FC1, cv::Scalar(0));
    size_t count = 0;
    for (const auto& image : images) {
        if (image.empty() || image.size() != images.front().size()) {
            continue;
        }
        cv::Mat asFloat;
        image.convertTo(asFloat, CV_32FC1);
        accum += asFloat;
        ++count;
    }
    if (count == 0) {
        return {};
    }
    accum /= static_cast<double>(count);
    cv::Mat background;
    accum.convertTo(background, CV_8UC1);
    return background;
}

std::string predictionFor(const ProcessedFrame& frame, int maskPixels)
{
    if (frame.validation.emptyFrameDiscarded || maskPixels == 0) {
        return "empty";
    }
    if (frame.validation.isValid) {
        return "valid_object";
    }
    return "non_empty_candidate";
}

FrameMetrics collectMetrics(const std::string& sampleId,
                            uint64_t rowIndex,
                            const std::string& caseType,
                            const std::string& sourcePath,
                            const ProcessedFrame& frame)
{
    FrameMetrics metrics;
    metrics.sampleId = sampleId;
    metrics.rowIndex = rowIndex;
    metrics.caseType = caseType;
    metrics.sourcePath = sourcePath;
    metrics.width = frame.originalImage.cols;
    metrics.height = frame.originalImage.rows;
    metrics.maskPixels = frame.processedImage.empty() ? 0 : cv::countNonZero(frame.processedImage);
    metrics.contourCount = frame.validation.allContours.size();
    metrics.innerContourCount = frame.validation.innerContourCount;
    metrics.emptyDiscarded = frame.validation.emptyFrameDiscarded;
    metrics.isValid = frame.validation.isValid;
    metrics.foregroundPixels = frame.validation.emptyFrameForegroundPixels;
    metrics.morphPixels = frame.validation.emptyFrameMorphPixels;
    metrics.roiOccupancy = frame.validation.emptyFrameRoiOccupancy;
    metrics.diffMean = frame.validation.emptyFrameDiffMean;
    metrics.thresholdStableRatio = frame.validation.emptyFrameThresholdStableRatio;
    metrics.backgroundShiftX = frame.validation.backgroundShiftX;
    metrics.backgroundShiftY = frame.validation.backgroundShiftY;
    metrics.backgroundAlignmentImprovement = frame.validation.backgroundAlignmentImprovement;
    metrics.prediction = predictionFor(frame, metrics.maskPixels);
    return metrics;
}

bool writeReviewSample(const std::filesystem::path& samplesDir,
                       const std::string& sampleId,
                       uint64_t rowIndex,
                       const std::string& caseType,
                       const std::string& sourcePath,
                       const ProcessedFrame& frame,
                       ReviewSample& out)
{
    std::filesystem::create_directories(samplesDir);
    out.metrics = collectMetrics(sampleId, rowIndex, caseType, sourcePath, frame);
    const auto inputPath = samplesDir / (sampleId + "-input.png");
    const auto maskPath = samplesDir / (sampleId + "-mask.png");
    const auto overlayPath = samplesDir / (sampleId + "-overlay.png");
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
    cv::putText(overlay, sampleId, cv::Point(8, 18), cv::FONT_HERSHEY_SIMPLEX,
                0.45, cv::Scalar(0, 255, 255), 1);
    cv::putText(overlay,
                out.metrics.prediction + " fg=" + std::to_string(out.metrics.foregroundPixels) +
                    " morph=" + std::to_string(out.metrics.morphPixels),
                cv::Point(8, 36), cv::FONT_HERSHEY_SIMPLEX, 0.38, cv::Scalar(0, 255, 255), 1);
    cv::putText(overlay,
                "shift=(" + std::to_string(out.metrics.backgroundShiftX) + "," +
                    std::to_string(out.metrics.backgroundShiftY) + ")",
                cv::Point(8, 54), cv::FONT_HERSHEY_SIMPLEX, 0.38, cv::Scalar(0, 255, 255), 1);
    if (!cv::imwrite(overlayPath.string(), overlay)) {
        std::cerr << "failed to write " << overlayPath << '\n';
        return false;
    }
    out.inputPath = inputPath.string();
    out.maskPath = maskPath.string();
    out.overlayPath = overlayPath.string();
    return true;
}

void writeFrameMetricsJson(std::ostream& out, const FrameMetrics& metrics, int indent)
{
    const std::string pad(static_cast<size_t>(indent), ' ');
    out << pad << "{\n";
    out << pad << "  \"sample_id\": \"" << jsonEscape(metrics.sampleId) << "\",\n";
    out << pad << "  \"row_index\": " << metrics.rowIndex << ",\n";
    out << pad << "  \"case_type\": \"" << jsonEscape(metrics.caseType) << "\",\n";
    out << pad << "  \"source_path\": \"" << jsonEscape(metrics.sourcePath) << "\",\n";
    out << pad << "  \"prediction\": \"" << jsonEscape(metrics.prediction) << "\",\n";
    out << pad << "  \"width\": " << metrics.width << ",\n";
    out << pad << "  \"height\": " << metrics.height << ",\n";
    out << pad << "  \"mask_pixels\": " << metrics.maskPixels << ",\n";
    out << pad << "  \"contour_count\": " << metrics.contourCount << ",\n";
    out << pad << "  \"inner_contour_count\": " << metrics.innerContourCount << ",\n";
    out << pad << "  \"empty_discarded\": " << (metrics.emptyDiscarded ? "true" : "false") << ",\n";
    out << pad << "  \"is_valid\": " << (metrics.isValid ? "true" : "false") << ",\n";
    out << pad << "  \"foreground_pixels\": " << metrics.foregroundPixels << ",\n";
    out << pad << "  \"morph_pixels\": " << metrics.morphPixels << ",\n";
    out << pad << "  \"roi_occupancy\": " << metrics.roiOccupancy << ",\n";
    out << pad << "  \"diff_mean\": " << metrics.diffMean << ",\n";
    out << pad << "  \"threshold_stable_ratio\": " << metrics.thresholdStableRatio << ",\n";
    out << pad << "  \"background_shift_x\": " << metrics.backgroundShiftX << ",\n";
    out << pad << "  \"background_shift_y\": " << metrics.backgroundShiftY << ",\n";
    out << pad << "  \"background_alignment_improvement\": " << metrics.backgroundAlignmentImprovement << "\n";
    out << pad << "}";
}

bool writeMetricsJson(const std::filesystem::path& outputPath,
                      const ProcessingConfig& config,
                      const std::vector<FrameMetrics>& hfMetrics,
                      const std::vector<ReviewSample>& reviewSamples)
{
    std::ofstream out(outputPath);
    if (!out) {
        std::cerr << "failed to write metrics JSON: " << outputPath << '\n';
        return false;
    }

    size_t emptyCount = 0;
    size_t nonEmptyCount = 0;
    size_t validCount = 0;
    size_t invalidNonEmptyCount = 0;
    for (const auto& metrics : hfMetrics) {
        if (metrics.prediction == "empty") {
            ++emptyCount;
        } else {
            ++nonEmptyCount;
        }
        if (metrics.isValid) {
            ++validCount;
        } else if (metrics.prediction != "empty") {
            ++invalidNonEmptyCount;
        }
    }

    out << std::fixed << std::setprecision(6);
    out << "{\n";
    out << "  \"dataset\": {\n";
    out << "    \"repo\": \"gavinlouuu/512x96stream\",\n";
    out << "    \"config\": \"default\",\n";
    out << "    \"split\": \"train\",\n";
    out << "    \"sample_count\": " << hfMetrics.size() << ",\n";
    out << "    \"labels_available\": false\n";
    out << "  },\n";
    out << "  \"config\": {\n";
    out << "    \"bg_subtract_threshold\": " << config.bg_subtract_threshold << ",\n";
    out << "    \"empty_frame_pixel_threshold\": " << config.empty_frame_pixel_threshold << ",\n";
    out << "    \"empty_frame_min_morph_pixels\": " << config.empty_frame_min_morph_pixels << ",\n";
    out << "    \"empty_frame_min_roi_occupancy\": " << config.empty_frame_min_roi_occupancy << ",\n";
    out << "    \"empty_frame_min_diff_mean\": " << config.empty_frame_min_diff_mean << ",\n";
    out << "    \"empty_frame_threshold_sensitivity_step\": " << config.empty_frame_threshold_sensitivity_step << ",\n";
    out << "    \"empty_frame_min_threshold_stable_ratio\": " << config.empty_frame_min_threshold_stable_ratio << ",\n";
    out << "    \"background_alignment_max_shift_px\": " << config.background_alignment_max_shift_px << ",\n";
    out << "    \"background_alignment_min_improvement\": " << config.background_alignment_min_improvement << "\n";
    out << "  },\n";
    out << "  \"aggregate\": {\n";
    out << "    \"hf_empty_discarded\": " << emptyCount << ",\n";
    out << "    \"hf_non_empty_candidates\": " << nonEmptyCount << ",\n";
    out << "    \"hf_valid_object_candidates\": " << validCount << ",\n";
    out << "    \"hf_invalid_non_empty_candidates\": " << invalidNonEmptyCount << ",\n";
    out << "    \"manual_review_sample_count\": " << reviewSamples.size() << "\n";
    out << "  },\n";
    out << "  \"review_samples\": [\n";
    for (size_t i = 0; i < reviewSamples.size(); ++i) {
        out << "    {\n";
        out << "      \"input_path\": \"" << jsonEscape(reviewSamples[i].inputPath) << "\",\n";
        out << "      \"mask_path\": \"" << jsonEscape(reviewSamples[i].maskPath) << "\",\n";
        out << "      \"overlay_path\": \"" << jsonEscape(reviewSamples[i].overlayPath) << "\",\n";
        out << "      \"metrics\": ";
        writeFrameMetricsJson(out, reviewSamples[i].metrics, 0);
        out << "\n    }";
        if (i + 1 < reviewSamples.size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ],\n";
    out << "  \"hf_frames\": [\n";
    for (size_t i = 0; i < hfMetrics.size(); ++i) {
        writeFrameMetricsJson(out, hfMetrics[i], 4);
        if (i + 1 < hfMetrics.size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ]\n";
    out << "}\n";
    return true;
}

std::optional<size_t> findFirstMatching(const std::vector<FrameMetrics>& metrics,
                                        const std::string& prediction,
                                        bool requireValid = false)
{
    for (size_t i = 0; i < metrics.size(); ++i) {
        if (metrics[i].prediction != prediction) {
            continue;
        }
        if (requireValid && !metrics[i].isValid) {
            continue;
        }
        return i;
    }
    return std::nullopt;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: " << argv[0] << " <output-dir> <hf-manifest.tsv>\n";
        return 2;
    }

    const std::filesystem::path outputDir = argv[1];
    const std::filesystem::path manifestPath = argv[2];
    const std::filesystem::path samplesDir = outputDir / "samples";
    std::filesystem::create_directories(outputDir);
    std::filesystem::create_directories(samplesDir);

    std::vector<ImageRecord> records;
    if (!readManifest(manifestPath, records)) {
        return 3;
    }

    std::vector<cv::Mat> images;
    if (!loadImages(records, images)) {
        return 4;
    }

    const cv::Mat hfBackground = buildMeanBackground(images);
    if (hfBackground.empty()) {
        std::cerr << "failed to build HF mean background\n";
        return 5;
    }
    if (!cv::imwrite((outputDir / "background_mean.png").string(), hfBackground)) {
        std::cerr << "failed to write background_mean.png\n";
        return 6;
    }

    ProcessingService service;
    const ProcessingConfig config = makeKin7Config();

    std::vector<ReviewSample> reviewSamples;
    const cv::Mat syntheticBackground = makeEdgeBackground(500);
    const std::vector<std::pair<std::string, cv::Mat>> syntheticCases{
        {"synthetic-edge-drift-empty", makeEdgeBackground(498)},
        {"synthetic-sparse-noise-empty", makeSparseNoiseOnBackground(syntheticBackground)},
        {"synthetic-known-good-ring", makeRingOnBackground(syntheticBackground)},
    };
    for (size_t i = 0; i < syntheticCases.size(); ++i) {
        const auto& [caseType, image] = syntheticCases[i];
        const ProcessingService::Roi roi{0, 0, image.cols, image.rows};
        const ProcessedFrame frame = service.computeProcessedFrame(
            image, syntheticBackground, config, roi, static_cast<uint64_t>(i), 0);
        ReviewSample sample;
        if (!writeReviewSample(samplesDir, caseType, static_cast<uint64_t>(i), caseType,
                               "synthetic", frame, sample)) {
            return 7;
        }
        reviewSamples.push_back(std::move(sample));
    }

    std::vector<FrameMetrics> hfMetrics;
    hfMetrics.reserve(images.size());
    const ProcessingService::Roi hfRoi{0, 0, images.front().cols, images.front().rows};
    for (size_t i = 0; i < images.size(); ++i) {
        const auto& record = records[i];
        const ProcessedFrame frame = service.computeProcessedFrame(
            images[i], hfBackground, config, hfRoi, record.rowIndex, 0);
        hfMetrics.push_back(collectMetrics(sampleIdForRow(record.rowIndex), record.rowIndex,
                                           record.caseType, record.path, frame));
    }

    std::vector<size_t> sampleIndices;
    if (const auto idx = findFirstMatching(hfMetrics, "empty")) {
        sampleIndices.push_back(*idx);
    }
    if (const auto idx = findFirstMatching(hfMetrics, "valid_object", true)) {
        sampleIndices.push_back(*idx);
    }
    if (const auto idx = findFirstMatching(hfMetrics, "non_empty_candidate")) {
        sampleIndices.push_back(*idx);
    }
    std::sort(sampleIndices.begin(), sampleIndices.end());
    sampleIndices.erase(std::unique(sampleIndices.begin(), sampleIndices.end()), sampleIndices.end());

    for (const size_t idx : sampleIndices) {
        const auto& record = records[idx];
        const ProcessedFrame frame = service.computeProcessedFrame(
            images[idx], hfBackground, config, hfRoi, record.rowIndex, 0);
        ReviewSample sample;
        if (!writeReviewSample(samplesDir, sampleIdForRow(record.rowIndex), record.rowIndex,
                               record.caseType, record.path, frame, sample)) {
            return 8;
        }
        reviewSamples.push_back(std::move(sample));
    }

    if (!writeMetricsJson(outputDir / "metrics.json", config, hfMetrics, reviewSamples)) {
        return 9;
    }

    size_t emptyCount = 0;
    size_t nonEmptyCount = 0;
    size_t validCount = 0;
    for (const auto& metrics : hfMetrics) {
        if (metrics.prediction == "empty") {
            ++emptyCount;
        } else {
            ++nonEmptyCount;
        }
        if (metrics.isValid) {
            ++validCount;
        }
    }

    std::cout << "KIN-7 evidence generated: hf_rows=" << hfMetrics.size()
              << " empty=" << emptyCount
              << " non_empty=" << nonEmptyCount
              << " valid=" << validCount
              << " review_samples=" << reviewSamples.size()
              << " metrics=" << (outputDir / "metrics.json") << '\n';
    return 0;
}

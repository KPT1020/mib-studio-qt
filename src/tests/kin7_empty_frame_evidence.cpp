#include "backend/services/ProcessingService.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
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

struct SampleSelection {
    std::string sampleId;
    std::string caseType;
    std::string expectedLabel;
    std::string prediction;
    size_t resultIndex{0};
    bool synthetic{false};
    ProcessedFrame frame;
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

std::string relativePath(const std::filesystem::path& root, const std::filesystem::path& path)
{
    std::error_code ec;
    const auto rel = std::filesystem::relative(path, root, ec);
    return ec ? path.generic_string() : rel.generic_string();
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
    return !records.empty();
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

cv::Mat buildMeanBackground(const std::vector<cv::Mat>& images)
{
    cv::Mat accumulator(images.front().rows, images.front().cols, CV_64FC1, cv::Scalar(0));
    size_t count = 0;
    for (const auto& image : images) {
        if (image.size() != images.front().size() || image.type() != CV_8UC1) {
            continue;
        }
        cv::Mat asDouble;
        image.convertTo(asDouble, CV_64FC1);
        accumulator += asDouble;
        ++count;
    }
    if (count == 0) {
        return cv::Mat{};
    }
    accumulator /= static_cast<double>(count);
    cv::Mat background;
    accumulator.convertTo(background, CV_8UC1);
    return background;
}

ProcessingConfig makeEvidenceConfig()
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
    config.enable_empty_frame_discard = true;
    config.empty_frame_min_roi_occupancy = 0.002;
    config.empty_frame_min_diff_energy = 1.0;
    config.empty_frame_threshold_sensitivity_delta = 8;
    config.empty_frame_min_threshold_retention = 0.25;
    config.empty_frame_min_morph_pixels = 250;
    config.empty_frame_min_morph_occupancy = 0.005;
    return config;
}

cv::Mat makeSyntheticRingFrame(int width = 512, int height = 96)
{
    cv::Mat image(height, width, CV_8UC1, cv::Scalar(0));
    const cv::Point center(width / 2, height / 2);
    cv::circle(image, center, 24, cv::Scalar(255), -1);
    cv::circle(image, center, 11, cv::Scalar(0), -1);
    return image;
}

cv::Mat makeSyntheticDriftNoiseFrame(int width = 512, int height = 96)
{
    cv::Mat image(height, width, CV_8UC1, cv::Scalar(0));
    std::mt19937 rng(7);
    std::uniform_int_distribution<int> xdist(0, width - 1);
    std::uniform_int_distribution<int> ydist(0, height - 1);
    std::uniform_int_distribution<int> vdist(20, 70);
    for (int i = 0; i < 650; ++i) {
        image.at<uchar>(ydist(rng), xdist(rng)) = static_cast<uchar>(vdist(rng));
    }
    return image;
}

std::string predictionForFrame(const ProcessedFrame& frame)
{
    return frame.validation.emptyFrameDiscarded ? "empty" : "non_empty";
}

int nonZeroMaskPixels(const ProcessedFrame& frame)
{
    return frame.processedImage.empty() ? 0 : cv::countNonZero(frame.processedImage);
}

size_t findBestEmptyCandidate(const std::vector<ProcessedFrame>& results)
{
    for (size_t i = 0; i < results.size(); ++i) {
        if (results[i].validation.emptyFrameDiscarded) {
            return i;
        }
    }
    return 0;
}

size_t findBestNonEmptyCandidate(const std::vector<ProcessedFrame>& results)
{
    size_t best = 0;
    int bestMaskPixels = -1;
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& frame = results[i];
        const int maskPixels = nonZeroMaskPixels(frame);
        if (!frame.validation.emptyFrameDiscarded &&
            (!frame.validation.allContours.empty() || frame.validation.isValid) &&
            maskPixels > bestMaskPixels) {
            best = i;
            bestMaskPixels = maskPixels;
        }
    }
    return best;
}

size_t findBorderOrAmbiguousCandidate(const std::vector<ProcessedFrame>& results)
{
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& frame = results[i];
        if (!frame.validation.emptyFrameDiscarded && !frame.validation.isValid && nonZeroMaskPixels(frame) > 0) {
            return i;
        }
    }
    return findBestNonEmptyCandidate(results);
}

bool writeSampleImages(const std::filesystem::path& outputDir,
                       const std::vector<ImageRecord>& records,
                       const SampleSelection& sample,
                       std::ostream& metricsJson)
{
    const auto sampleDir = outputDir / "samples";
    std::filesystem::create_directories(sampleDir);

    const auto inputPath = sampleDir / (sample.sampleId + "-input.png");
    const auto maskPath = sampleDir / (sample.sampleId + "-mask.png");
    const auto overlayPath = sampleDir / (sample.sampleId + "-overlay.png");

    if (!cv::imwrite(inputPath.string(), sample.frame.originalImage)) {
        std::cerr << "failed to write " << inputPath << '\n';
        return false;
    }
    if (!cv::imwrite(maskPath.string(), sample.frame.processedImage)) {
        std::cerr << "failed to write " << maskPath << '\n';
        return false;
    }

    cv::Mat overlay;
    cv::cvtColor(sample.frame.originalImage, overlay, cv::COLOR_GRAY2BGR);
    if (!sample.frame.validation.allContours.empty()) {
        cv::drawContours(overlay, sample.frame.validation.allContours, -1, cv::Scalar(0, 255, 0), 1);
    }
    const std::string label = sample.sampleId + " pred=" + sample.prediction;
    cv::putText(overlay, label, cv::Point(8, 18), cv::FONT_HERSHEY_SIMPLEX, 0.42, cv::Scalar(0, 255, 255), 1);
    if (!cv::imwrite(overlayPath.string(), overlay)) {
        std::cerr << "failed to write " << overlayPath << '\n';
        return false;
    }

    const auto& validation = sample.frame.validation;
    const uint64_t rowIndex = sample.synthetic ? 0 : records[sample.resultIndex].rowIndex;
    const std::string sourcePath = sample.synthetic ? "synthetic" : records[sample.resultIndex].path;

    metricsJson << "    {\n";
    metricsJson << "      \"sample_id\": \"" << jsonEscape(sample.sampleId) << "\",\n";
    metricsJson << "      \"case_type\": \"" << jsonEscape(sample.caseType) << "\",\n";
    metricsJson << "      \"source\": \"" << (sample.synthetic ? "synthetic" : "hf") << "\",\n";
    metricsJson << "      \"row_index\": " << rowIndex << ",\n";
    metricsJson << "      \"source_path\": \"" << jsonEscape(sourcePath) << "\",\n";
    metricsJson << "      \"expected_label\": \"" << jsonEscape(sample.expectedLabel) << "\",\n";
    metricsJson << "      \"prediction\": \"" << jsonEscape(sample.prediction) << "\",\n";
    metricsJson << "      \"empty_frame_discarded\": " << (validation.emptyFrameDiscarded ? "true" : "false") << ",\n";
    metricsJson << "      \"is_valid\": " << (validation.isValid ? "true" : "false") << ",\n";
    metricsJson << "      \"threshold_pixels\": " << validation.emptyFrameThresholdPixels << ",\n";
    metricsJson << "      \"strong_threshold_pixels\": " << validation.emptyFrameStrongThresholdPixels << ",\n";
    metricsJson << "      \"morph_pixels\": " << validation.emptyFrameMorphPixels << ",\n";
    metricsJson << "      \"roi_occupancy\": " << validation.emptyFrameRoiOccupancy << ",\n";
    metricsJson << "      \"strong_occupancy\": " << validation.emptyFrameStrongOccupancy << ",\n";
    metricsJson << "      \"threshold_retention\": " << validation.emptyFrameThresholdRetention << ",\n";
    metricsJson << "      \"diff_energy\": " << validation.emptyFrameDiffEnergy << ",\n";
    metricsJson << "      \"contour_count\": " << validation.allContours.size() << ",\n";
    metricsJson << "      \"inner_contour_count\": " << validation.innerContourCount << ",\n";
    metricsJson << "      \"area\": " << validation.area << ",\n";
    metricsJson << "      \"ring_ratio\": " << validation.ringRatio << ",\n";
    metricsJson << "      \"input_path\": \"" << jsonEscape(relativePath(outputDir, inputPath)) << "\",\n";
    metricsJson << "      \"mask_path\": \"" << jsonEscape(relativePath(outputDir, maskPath)) << "\",\n";
    metricsJson << "      \"overlay_path\": \"" << jsonEscape(relativePath(outputDir, overlayPath)) << "\"\n";
    metricsJson << "    }";
    return true;
}

bool writeMetricsJson(const std::filesystem::path& outputDir,
                      const std::vector<ImageRecord>& records,
                      const std::vector<ProcessedFrame>& results,
                      const std::vector<SampleSelection>& samples,
                      const ProcessingConfig& config,
                      const cv::Mat& background)
{
    const auto metricsPath = outputDir / "metrics.json";
    std::ofstream out(metricsPath);
    if (!out) {
        std::cerr << "failed to open " << metricsPath << '\n';
        return false;
    }

    size_t emptyDiscarded = 0;
    size_t nonEmpty = 0;
    size_t valid = 0;
    size_t contourFrames = 0;
    size_t highMorphFrames = 0;
    for (const auto& frame : results) {
        if (frame.validation.emptyFrameDiscarded) {
            ++emptyDiscarded;
        } else {
            ++nonEmpty;
        }
        if (frame.validation.isValid) {
            ++valid;
        }
        if (!frame.validation.allContours.empty()) {
            ++contourFrames;
        }
        if (frame.validation.emptyFrameMorphPixels >= config.empty_frame_min_morph_pixels) {
            ++highMorphFrames;
        }
    }

    out << std::fixed << std::setprecision(6);
    out << "{\n";
    out << "  \"dataset\": {\n";
    out << "    \"repo\": \"gavinlouuu/512x96stream\",\n";
    out << "    \"config\": \"default\",\n";
    out << "    \"split\": \"train\",\n";
    out << "    \"rows_loaded\": " << records.size() << ",\n";
    out << "    \"label_columns\": []\n";
    out << "  },\n";
    out << "  \"background\": {\n";
    out << "    \"method\": \"mean_all_hf_images\",\n";
    out << "    \"path\": \"background_mean.png\",\n";
    out << "    \"width\": " << background.cols << ",\n";
    out << "    \"height\": " << background.rows << "\n";
    out << "  },\n";
    out << "  \"config\": {\n";
    out << "    \"gaussian_blur_size\": " << config.gaussian_blur_size << ",\n";
    out << "    \"bg_subtract_threshold\": " << config.bg_subtract_threshold << ",\n";
    out << "    \"morph_kernel_size\": " << config.morph_kernel_size << ",\n";
    out << "    \"morph_iterations\": " << config.morph_iterations << ",\n";
    out << "    \"empty_frame_pixel_threshold\": " << config.empty_frame_pixel_threshold << ",\n";
    out << "    \"empty_frame_min_roi_occupancy\": " << config.empty_frame_min_roi_occupancy << ",\n";
    out << "    \"empty_frame_min_diff_energy\": " << config.empty_frame_min_diff_energy << ",\n";
    out << "    \"empty_frame_threshold_sensitivity_delta\": " << config.empty_frame_threshold_sensitivity_delta << ",\n";
    out << "    \"empty_frame_min_threshold_retention\": " << config.empty_frame_min_threshold_retention << ",\n";
    out << "    \"empty_frame_min_morph_pixels\": " << config.empty_frame_min_morph_pixels << ",\n";
    out << "    \"empty_frame_min_morph_occupancy\": " << config.empty_frame_min_morph_occupancy << "\n";
    out << "  },\n";
    out << "  \"aggregate\": {\n";
    out << "    \"empty_discarded\": " << emptyDiscarded << ",\n";
    out << "    \"non_empty_candidates\": " << nonEmpty << ",\n";
    out << "    \"valid_nested_object_candidates\": " << valid << ",\n";
    out << "    \"frames_with_contours\": " << contourFrames << ",\n";
    out << "    \"frames_with_morph_evidence\": " << highMorphFrames << "\n";
    out << "  },\n";
    out << "  \"manual_review_set\": [\n";
    for (size_t i = 0; i < samples.size(); ++i) {
        if (!writeSampleImages(outputDir, records, samples[i], out)) {
            return false;
        }
        if (i + 1 < samples.size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ],\n";
    out << "  \"per_frame_metrics\": [\n";
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& validation = results[i].validation;
        out << "    {";
        out << "\"row_index\": " << records[i].rowIndex << ", ";
        out << "\"prediction\": \"" << predictionForFrame(results[i]) << "\", ";
        out << "\"empty_frame_discarded\": " << (validation.emptyFrameDiscarded ? "true" : "false") << ", ";
        out << "\"is_valid\": " << (validation.isValid ? "true" : "false") << ", ";
        out << "\"threshold_pixels\": " << validation.emptyFrameThresholdPixels << ", ";
        out << "\"morph_pixels\": " << validation.emptyFrameMorphPixels << ", ";
        out << "\"diff_energy\": " << validation.emptyFrameDiffEnergy << ", ";
        out << "\"threshold_retention\": " << validation.emptyFrameThresholdRetention << ", ";
        out << "\"contour_count\": " << validation.allContours.size() << ", ";
        out << "\"inner_contour_count\": " << validation.innerContourCount;
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
    std::filesystem::create_directories(outputDir / "samples");

    std::vector<ImageRecord> records;
    if (!readManifest(manifestPath, records)) {
        return 3;
    }

    std::vector<cv::Mat> images;
    if (!loadImages(records, images)) {
        return 4;
    }
    const cv::Mat background = buildMeanBackground(images);
    if (background.empty()) {
        std::cerr << "failed to build mean background\n";
        return 5;
    }
    if (!cv::imwrite((outputDir / "background_mean.png").string(), background)) {
        std::cerr << "failed to write mean background\n";
        return 6;
    }

    ProcessingService service;
    const ProcessingConfig config = makeEvidenceConfig();
    std::vector<ProcessedFrame> results;
    results.reserve(images.size());
    for (size_t i = 0; i < images.size(); ++i) {
        results.emplace_back(service.computeProcessedFrame(images[i],
                                                           background,
                                                           config,
                                                           ProcessingService::Roi{0, 0, 0, 0},
                                                           records[i].rowIndex,
                                                           0));
    }

    ProcessingService syntheticService;
    const cv::Mat syntheticBackground(96, 512, CV_8UC1, cv::Scalar(0));
    SampleSelection syntheticNoise;
    syntheticNoise.sampleId = "synthetic-drift-noise";
    syntheticNoise.caseType = "empty_noise_threshold_drift";
    syntheticNoise.expectedLabel = "empty";
    syntheticNoise.synthetic = true;
    syntheticNoise.frame = syntheticService.computeProcessedFrame(makeSyntheticDriftNoiseFrame(),
                                                                  syntheticBackground,
                                                                  config,
                                                                  ProcessingService::Roi{0, 0, 0, 0},
                                                                  900001,
                                                                  0);
    syntheticNoise.prediction = predictionForFrame(syntheticNoise.frame);

    SampleSelection syntheticRing;
    syntheticRing.sampleId = "synthetic-known-good-ring";
    syntheticRing.caseType = "known_good_non_empty_regression";
    syntheticRing.expectedLabel = "non_empty";
    syntheticRing.synthetic = true;
    syntheticRing.frame = syntheticService.computeProcessedFrame(makeSyntheticRingFrame(),
                                                                 syntheticBackground,
                                                                 config,
                                                                 ProcessingService::Roi{0, 0, 0, 0},
                                                                 900002,
                                                                 0);
    syntheticRing.prediction = predictionForFrame(syntheticRing.frame);

    const size_t emptyIndex = findBestEmptyCandidate(results);
    SampleSelection hfEmpty;
    hfEmpty.sampleId = "hf-empty-candidate-row-" + std::to_string(records[emptyIndex].rowIndex);
    hfEmpty.caseType = "hf_empty_candidate_unlabeled";
    hfEmpty.expectedLabel = "manual_review_unlabeled";
    hfEmpty.prediction = predictionForFrame(results[emptyIndex]);
    hfEmpty.resultIndex = emptyIndex;
    hfEmpty.frame = results[emptyIndex];

    const size_t nonEmptyIndex = findBestNonEmptyCandidate(results);
    SampleSelection hfNonEmpty;
    hfNonEmpty.sampleId = "hf-non-empty-candidate-row-" + std::to_string(records[nonEmptyIndex].rowIndex);
    hfNonEmpty.caseType = "hf_non_empty_candidate_unlabeled";
    hfNonEmpty.expectedLabel = "manual_review_unlabeled";
    hfNonEmpty.prediction = predictionForFrame(results[nonEmptyIndex]);
    hfNonEmpty.resultIndex = nonEmptyIndex;
    hfNonEmpty.frame = results[nonEmptyIndex];

    const size_t ambiguousIndex = findBorderOrAmbiguousCandidate(results);
    SampleSelection hfAmbiguous;
    hfAmbiguous.sampleId = "hf-ambiguous-candidate-row-" + std::to_string(records[ambiguousIndex].rowIndex);
    hfAmbiguous.caseType = "hf_ambiguous_or_invalid_candidate_unlabeled";
    hfAmbiguous.expectedLabel = "manual_review_unlabeled";
    hfAmbiguous.prediction = predictionForFrame(results[ambiguousIndex]);
    hfAmbiguous.resultIndex = ambiguousIndex;
    hfAmbiguous.frame = results[ambiguousIndex];

    const std::vector<SampleSelection> samples{
        syntheticNoise,
        syntheticRing,
        hfEmpty,
        hfNonEmpty,
        hfAmbiguous,
    };

    if (!writeMetricsJson(outputDir, records, results, samples, config, background)) {
        return 7;
    }

    size_t emptyDiscarded = 0;
    for (const auto& result : results) {
        if (result.validation.emptyFrameDiscarded) {
            ++emptyDiscarded;
        }
    }
    std::cout << "KIN-7 evidence generated at " << outputDir << '\n';
    std::cout << "rows_loaded=" << records.size()
              << " empty_discarded=" << emptyDiscarded
              << " non_empty_candidates=" << (records.size() - emptyDiscarded)
              << '\n';
    return 0;
}

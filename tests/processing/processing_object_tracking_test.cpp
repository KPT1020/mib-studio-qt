#include "backend/recording/Hdf5Service.h"
#include "backend/processing/ProcessingService.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace
{
cv::Mat makeRingFrame(const std::vector<cv::Point>& centers)
{
    cv::Mat image(96, 180, CV_8UC1, cv::Scalar(0));
    for (const auto& center : centers)
    {
        cv::circle(image, center, 20, cv::Scalar(255), cv::FILLED);
        cv::circle(image, center, 9, cv::Scalar(0), cv::FILLED);
    }
    return image;
}

backend::services::ProcessingConfig permissiveTrackingConfig()
{
    backend::services::ProcessingConfig config;
    config.gaussian_blur_size = 1;
    config.bg_subtract_threshold = 127;
    config.morph_kernel_size = 1;
    config.morph_iterations = 1;
    config.enable_area_range_check = false;
    config.enable_ring_ratio_check = false;
    config.enable_deformability_range_check = false;
    config.enable_area_ratio_check = false;
    config.enable_border_check = true;
    config.require_single_inner_contour = true;
    return config;
}

backend::services::ProcessingConfig hfStreamTrackingConfig()
{
    backend::services::ProcessingConfig config;
    config.gaussian_blur_size = 3;
    config.bg_subtract_threshold = 6;
    config.morph_kernel_size = 3;
    config.morph_iterations = 1;
    config.enable_area_range_check = false;
    config.enable_ring_ratio_check = false;
    config.enable_deformability_range_check = false;
    config.enable_area_ratio_check = false;
    config.enable_border_check = false;
    config.require_single_inner_contour = false;
    return config;
}

std::string makeTempPath()
{
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<unsigned long long> dist;
    return (std::filesystem::temp_directory_path() /
            ("mib_processing_object_tracking_" + std::to_string(dist(gen)) + ".h5"))
        .string();
}

std::vector<backend::services::ProcessedFrame> validFrames(
    const std::vector<backend::services::ProcessedFrame>& frames)
{
    std::vector<backend::services::ProcessedFrame> out;
    for (const auto& frame : frames)
    {
        if (frame.validation.isValid)
        {
            out.push_back(frame);
        }
    }
    return out;
}

std::vector<backend::services::ProcessedFrame> invalidFrames(
    const std::vector<backend::services::ProcessedFrame>& frames)
{
    std::vector<backend::services::ProcessedFrame> out;
    for (const auto& frame : frames)
    {
        if (!frame.validation.isValid)
        {
            out.push_back(frame);
        }
    }
    return out;
}

struct ReviewSample
{
    std::string id;
    std::string caseType;
    std::string expected;
    cv::Mat input;
    cv::Mat processedMask;
    int validDetections{0};
};

struct HfStreamEvidence
{
    bool available{false};
    int rawValidObservations{0};
    std::vector<int> apiRows;
    std::vector<int> viewerCells;
    std::vector<cv::Mat> inputFrames;
    std::vector<backend::services::ProcessedFrame> tracks;
};

std::string jsonEscape(const std::string& value)
{
    std::string out;
    out.reserve(value.size());
    for (const char ch : value)
    {
        switch (ch)
        {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        default:
            out += ch;
            break;
        }
    }
    return out;
}

std::string samplePath(const std::string& id, const std::string& suffix)
{
    return id + "_" + suffix + ".png";
}

void writeSampleArtifact(const std::filesystem::path& dir, const ReviewSample& sample)
{
    cv::imwrite((dir / samplePath(sample.id, "input")).string(), sample.input);
    cv::imwrite((dir / samplePath(sample.id, "mask")).string(), sample.processedMask);

    if (sample.input.empty())
    {
        return;
    }

    cv::Mat overlayBase;
    cv::cvtColor(sample.input, overlayBase, cv::COLOR_GRAY2BGR);
    cv::Mat overlay;
    constexpr double kOverlayScale = 3.0;
    cv::resize(overlayBase, overlay, cv::Size(), kOverlayScale, kOverlayScale, cv::INTER_NEAREST);

    if (!sample.processedMask.empty())
    {
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(sample.processedMask.clone(), contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        for (const auto& contour : contours)
        {
            const cv::Rect box = cv::boundingRect(contour);
            cv::rectangle(overlay,
                          cv::Rect(static_cast<int>(box.x * kOverlayScale),
                                   static_cast<int>(box.y * kOverlayScale),
                                   static_cast<int>(box.width * kOverlayScale),
                                   static_cast<int>(box.height * kOverlayScale)),
                          cv::Scalar(0, 220, 255), 2);
        }
    }

    cv::Mat canvas(overlay.rows + 54, overlay.cols, overlay.type(), cv::Scalar(0, 0, 0));
    overlay.copyTo(canvas(cv::Rect(0, 0, overlay.cols, overlay.rows)));
    std::ostringstream label;
    label << sample.id << " detections " << sample.validDetections;
    cv::putText(canvas, label.str(), cv::Point(10, overlay.rows + 22),
                cv::FONT_HERSHEY_SIMPLEX, 0.58, cv::Scalar(0, 220, 255), 1, cv::LINE_AA);
    cv::putText(canvas, sample.caseType, cv::Point(10, overlay.rows + 46),
                cv::FONT_HERSHEY_SIMPLEX, 0.52, cv::Scalar(220, 220, 220), 1, cv::LINE_AA);
    cv::imwrite((dir / samplePath(sample.id, "overlay")).string(), canvas);
}

void writeTrackingOverlay(const std::filesystem::path& path,
                          const cv::Mat& inputFrame,
                          const std::vector<backend::services::ProcessedFrame>& tracks)
{
    if (inputFrame.empty())
    {
        return;
    }

    cv::Mat overlayBase;
    cv::cvtColor(inputFrame, overlayBase, cv::COLOR_GRAY2BGR);
    cv::Mat overlay;
    constexpr double kOverlayScale = 3.0;
    cv::resize(overlayBase, overlay, cv::Size(), kOverlayScale, kOverlayScale, cv::INTER_NEAREST);
    cv::Mat canvas(overlay.rows + 28 + static_cast<int>(tracks.size()) * 22,
                   overlay.cols, overlay.type(), cv::Scalar(0, 0, 0));
    overlay.copyTo(canvas(cv::Rect(0, 0, overlay.cols, overlay.rows)));
    const std::vector<cv::Scalar> colors{
        cv::Scalar(0, 220, 255),
        cv::Scalar(40, 210, 80),
        cv::Scalar(255, 120, 40),
    };
    for (size_t i = 0; i < tracks.size(); ++i)
    {
        const auto& val = tracks[i].validation;
        const cv::Scalar color = colors[i % colors.size()];
        cv::rectangle(canvas,
                      cv::Rect(static_cast<int>(val.bboxX * kOverlayScale),
                               static_cast<int>(val.bboxY * kOverlayScale),
                               static_cast<int>(val.bboxWidth * kOverlayScale),
                               static_cast<int>(val.bboxHeight * kOverlayScale)),
                      color, 2);
        std::ostringstream label;
        label << "track " << val.trackId << " frames "
              << val.trackFirstFrame << "-" << val.trackLastFrame
              << " obs " << val.trackObservationCount;
        cv::putText(canvas, label.str(),
                    cv::Point(10, overlay.rows + 22 + static_cast<int>(i) * 22),
                    cv::FONT_HERSHEY_SIMPLEX, 0.58, color, 1, cv::LINE_AA);
    }
    cv::imwrite(path.string(), canvas);
}

bool readHfRowImage(const std::filesystem::path& dir,
                    int row,
                    cv::Mat& out,
                    std::ostream& err)
{
    const auto path = dir / ("row-" + std::to_string(row) + ".jpg");
    out = cv::imread(path.string(), cv::IMREAD_GRAYSCALE);
    if (out.empty())
    {
        err << "failed to read HF sample image " << path << "\n";
        return false;
    }
    return true;
}

bool readHfRowImages(const std::filesystem::path& dir,
                     const std::vector<int>& rows,
                     std::vector<cv::Mat>& out,
                     std::ostream& err)
{
    out.clear();
    out.reserve(rows.size());
    for (const int row : rows)
    {
        cv::Mat image;
        if (!readHfRowImage(dir, row, image, err))
        {
            return false;
        }
        out.push_back(std::move(image));
    }
    return true;
}

int countRawValidObservations(backend::services::ProcessingService& service,
                              const std::vector<cv::Mat>& frames,
                              const backend::services::ProcessingConfig& config,
                              const cv::Mat& background,
                              const backend::services::ProcessingService::Roi& roi)
{
    int count = 0;
    for (const auto& frame : frames)
    {
        count += static_cast<int>(validFrames(service.processBatch({frame}, config, background, roi)).size());
    }
    return count;
}

bool loadAndValidateHfStreamEvidence(backend::services::ProcessingService& service,
                                     HfStreamEvidence& evidence,
                                     std::ostream& err)
{
    const char* sampleDir = std::getenv("KIN9_HF_SAMPLE_DIR");
    if (sampleDir == nullptr || std::string(sampleDir).empty())
    {
        std::cout << "KIN9_HF_SAMPLE_DIR not set; skipping HF 512x96stream sample assertion\n";
        return true;
    }

    const std::filesystem::path dir(sampleDir);
    evidence.available = true;
    evidence.apiRows = {20, 21, 22};
    evidence.viewerCells = {21, 22, 23};
    if (!readHfRowImages(dir, evidence.apiRows, evidence.inputFrames, err))
    {
        return false;
    }

    cv::Mat background;
    if (!readHfRowImage(dir, 24, background, err))
    {
        return false;
    }

    const auto config = hfStreamTrackingConfig();
    const backend::services::ProcessingService::Roi roi{0, 8, 512, 70};
    evidence.rawValidObservations = countRawValidObservations(service, evidence.inputFrames, config, background, roi);
    const auto hfResults = service.processBatch(evidence.inputFrames, config, background, roi);
    evidence.tracks = validFrames(hfResults);

    if (evidence.rawValidObservations != 3)
    {
        err << "expected HF viewer cells 21-23 to produce 3 raw valid observations, got "
            << evidence.rawValidObservations << "\n";
        return false;
    }
    if (evidence.tracks.size() != 1)
    {
        err << "expected HF viewer cells 21-23 to dedupe to 1 track, got "
            << evidence.tracks.size() << " tracks from " << hfResults.size()
            << " total records\n";
        return false;
    }

    const auto& val = evidence.tracks.front().validation;
    if (val.trackId != 1 || val.trackFirstFrame != 0 || val.trackLastFrame != 2 ||
        val.trackObservationCount != 3)
    {
        err << "bad HF stream track metadata: trackId=" << val.trackId
            << " first=" << val.trackFirstFrame
            << " last=" << val.trackLastFrame
            << " observations=" << val.trackObservationCount << "\n";
        return false;
    }

    std::cout << "HF 512x96stream viewer cells 21-23 deduped "
              << evidence.rawValidObservations << " raw observations to "
              << evidence.tracks.size() << " track\n";
    return true;
}

void writeReviewArtifacts(const std::filesystem::path& dir,
                          const std::vector<cv::Mat>& inputFrames,
                          const std::vector<backend::services::ProcessedFrame>& tracks,
                          const std::vector<cv::Mat>& reverseInputFrames,
                          const std::vector<backend::services::ProcessedFrame>& reverseTracks,
                          const HfStreamEvidence& hfEvidence,
                          const std::vector<ReviewSample>& samples)
{
    std::filesystem::create_directories(dir);

    for (const auto& sample : samples)
    {
        writeSampleArtifact(dir, sample);
    }

    for (size_t i = 0; i < inputFrames.size(); ++i)
    {
        std::ostringstream name;
        name << "input_frame_" << std::setw(3) << std::setfill('0') << i << ".png";
        cv::imwrite((dir / name.str()).string(), inputFrames[i]);
    }

    for (size_t i = 0; i < reverseInputFrames.size(); ++i)
    {
        std::ostringstream name;
        name << "reverse_motion_frame_" << std::setw(3) << std::setfill('0') << i << ".png";
        cv::imwrite((dir / name.str()).string(), reverseInputFrames[i]);
    }

    for (size_t i = 0; i < hfEvidence.inputFrames.size(); ++i)
    {
        std::ostringstream name;
        name << "hf_512x96stream_cell_" << std::setw(3) << std::setfill('0')
             << (i < hfEvidence.viewerCells.size() ? hfEvidence.viewerCells[i] : static_cast<int>(i + 1))
             << "_input.png";
        cv::imwrite((dir / name.str()).string(), hfEvidence.inputFrames[i]);
    }

    if (!tracks.empty())
    {
        cv::imwrite((dir / "processed_mask_track_001.png").string(), tracks.front().processedImage);
    }

    writeTrackingOverlay(dir / "tracking_overlay.png", inputFrames.front(), tracks);
    writeTrackingOverlay(dir / "reverse_motion_overlay.png", reverseInputFrames.front(), reverseTracks);
    if (hfEvidence.available && !hfEvidence.inputFrames.empty())
    {
        writeTrackingOverlay(dir / "hf_512x96stream_overlay.png",
                             hfEvidence.inputFrames.front(),
                             hfEvidence.tracks);
    }

    std::ofstream metrics(dir / "metrics.json");
    metrics << "{\n"
            << "  \"input_frames\": " << inputFrames.size() << ",\n"
            << "  \"raw_valid_observations\": 6,\n"
            << "  \"deduped_valid_tracks\": " << tracks.size() << ",\n"
            << "  \"duplicate_detections_suppressed\": " << (6 - tracks.size()) << ",\n"
            << "  \"reverse_motion_input_frames\": " << reverseInputFrames.size() << ",\n"
            << "  \"reverse_motion_raw_observations\": 2,\n"
            << "  \"reverse_motion_tracks\": " << reverseTracks.size() << ",\n"
            << "  \"reverse_motion_duplicate_detections_suppressed\": " << (2 - reverseTracks.size()) << ",\n"
            << "  \"hf_stream\": {\n"
            << "    \"dataset\": \"gavinlouuu/512x96stream\",\n"
            << "    \"config\": \"default\",\n"
            << "    \"split\": \"train\",\n"
            << "    \"sample_note\": \"viewer cells are one-indexed; API row_idx values are zero-indexed\",\n"
            << "    \"available\": " << (hfEvidence.available ? "true" : "false") << ",\n"
            << "    \"viewer_cells\": [";
    for (size_t i = 0; i < hfEvidence.viewerCells.size(); ++i)
    {
        metrics << hfEvidence.viewerCells[i];
        if (i + 1 != hfEvidence.viewerCells.size())
        {
            metrics << ", ";
        }
    }
    metrics << "],\n"
            << "    \"api_rows\": [";
    for (size_t i = 0; i < hfEvidence.apiRows.size(); ++i)
    {
        metrics << hfEvidence.apiRows[i];
        if (i + 1 != hfEvidence.apiRows.size())
        {
            metrics << ", ";
        }
    }
    metrics << "],\n"
            << "    \"raw_valid_observations\": " << hfEvidence.rawValidObservations << ",\n"
            << "    \"deduped_valid_tracks\": " << hfEvidence.tracks.size() << ",\n"
            << "    \"duplicate_detections_suppressed\": "
            << (hfEvidence.rawValidObservations - static_cast<int>(hfEvidence.tracks.size())) << ",\n"
            << "    \"tracks\": [\n";
    for (size_t i = 0; i < hfEvidence.tracks.size(); ++i)
    {
        const auto& val = hfEvidence.tracks[i].validation;
        metrics << "      {\"track_id\": " << val.trackId
                << ", \"first_frame\": " << val.trackFirstFrame
                << ", \"last_frame\": " << val.trackLastFrame
                << ", \"observations\": " << val.trackObservationCount << "}";
        if (i + 1 != hfEvidence.tracks.size())
        {
            metrics << ",";
        }
        metrics << "\n";
    }
    metrics << "    ]\n"
            << "  },\n"
            << "  \"sample_cases\": [\n";
    for (size_t i = 0; i < samples.size(); ++i)
    {
        const auto& sample = samples[i];
        metrics << "    {\"id\": \"" << jsonEscape(sample.id)
                << "\", \"case_type\": \"" << jsonEscape(sample.caseType)
                << "\", \"valid_detections\": " << sample.validDetections
                << ", \"input\": \"" << samplePath(sample.id, "input")
                << "\", \"mask\": \"" << samplePath(sample.id, "mask")
                << "\", \"overlay\": \"" << samplePath(sample.id, "overlay")
                << "\", \"expected\": \"" << jsonEscape(sample.expected) << "\"}";
        if (i + 1 != samples.size())
        {
            metrics << ",";
        }
        metrics << "\n";
    }
    metrics << "  ],\n"
            << "  \"tracks\": [\n";
    for (size_t i = 0; i < tracks.size(); ++i)
    {
        const auto& val = tracks[i].validation;
        metrics << "    {\"track_id\": " << val.trackId
                << ", \"first_frame\": " << val.trackFirstFrame
                << ", \"last_frame\": " << val.trackLastFrame
                << ", \"observations\": " << val.trackObservationCount << "}";
        if (i + 1 != tracks.size())
        {
            metrics << ",";
        }
        metrics << "\n";
    }
    metrics << "  ],\n"
            << "  \"reverse_motion_track_details\": [\n";
    for (size_t i = 0; i < reverseTracks.size(); ++i)
    {
        const auto& val = reverseTracks[i].validation;
        metrics << "    {\"track_id\": " << val.trackId
                << ", \"first_frame\": " << val.trackFirstFrame
                << ", \"last_frame\": " << val.trackLastFrame
                << ", \"observations\": " << val.trackObservationCount << "}";
        if (i + 1 != reverseTracks.size())
        {
            metrics << ",";
        }
        metrics << "\n";
    }
    metrics << "  ]\n"
            << "}\n";

    std::ofstream manifest(dir / "README.md");
    manifest << "# KIN-9 Review Bundle\n\n"
             << "- `sample-*_input.png`, `sample-*_mask.png`, `sample-*_overlay.png`: representative per-sample input, processed mask, and contour overlay triples.\n"
             << "- `input_frame_*.png`: synthetic HF-stream-style frame sequence with two moving ring objects and one empty gap frame.\n"
             << "- `reverse_motion_frame_*.png`: chronological right-to-left overlap sequence that must not deduplicate into one track.\n"
             << "- `hf_512x96stream_cell_*_input.png`: downloaded Hugging Face sample frames for viewer cells 21-23 from `gavinlouuu/512x96stream`.\n"
             << "- `processed_mask_track_001.png`: processed mask generated by `ProcessingService` for the first retained track record.\n"
             << "- `tracking_overlay.png`: retained track records with stable IDs and first/last frame spans overlaid on the first input frame.\n"
             << "- `reverse_motion_overlay.png`: reverse-motion records with separate one-observation tracks.\n"
             << "- `hf_512x96stream_overlay.png`: retained HF stream track with stable ID and first/last frame span.\n"
             << "- `metrics.json`: raw observation count, deduplicated track count, suppressed duplicate count, reverse-motion track count, and per-track spans.\n\n"
             << "Regenerate visuals with `KIN9_HF_SAMPLE_DIR=review_artifacts/KIN-9/hf-512x96stream KIN9_REVIEW_BUNDLE=review_artifacts/KIN-9 ./build/linux-backend/mib_processing_object_tracking_test`.\n"
             << "Run the full local bundle flow with `./review_artifacts/KIN-9/regenerate.sh`.\n";
}
} // namespace

int main()
{
    backend::services::ProcessingService service;
    const auto config = permissiveTrackingConfig();
    const std::vector<cv::Mat> frames{
        makeRingFrame({cv::Point(42, 48), cv::Point(114, 48)}),
        makeRingFrame({cv::Point(50, 48), cv::Point(122, 48)}),
        makeRingFrame({}),
        makeRingFrame({cv::Point(66, 48), cv::Point(138, 48)}),
    };

    const auto results = service.processBatch(frames, config);
    const auto valid = validFrames(results);
    const auto invalid = invalidFrames(results);

    if (valid.size() != 2)
    {
        std::cerr << "expected 2 deduplicated valid object tracks, got " << valid.size()
                  << " from " << results.size() << " total records\n";
        return 1;
    }
    if (invalid.size() != 1)
    {
        std::cerr << "expected one empty gap frame to remain invalid, got " << invalid.size() << "\n";
        return 2;
    }

    for (size_t i = 0; i < valid.size(); ++i)
    {
        const auto& val = valid[i].validation;
        const int expectedTrackId = static_cast<int>(i + 1);
        if (val.trackId != expectedTrackId ||
            val.trackFirstFrame != 0 ||
            val.trackLastFrame != 3 ||
            val.trackObservationCount != 3)
        {
            std::cerr << "bad track metadata for result " << i
                      << ": trackId=" << val.trackId
                      << " first=" << val.trackFirstFrame
                      << " last=" << val.trackLastFrame
                      << " observations=" << val.trackObservationCount << "\n";
            return 3;
        }
        if (val.bboxWidth <= 0.0 || val.bboxHeight <= 0.0 ||
            val.centroidX <= 0.0 || val.centroidY <= 0.0)
        {
            std::cerr << "track " << expectedTrackId << " missing bbox/centroid geometry\n";
            return 4;
        }
    }

    const std::vector<cv::Mat> reverseFrames{
        makeRingFrame({cv::Point(84, 48)}),
        makeRingFrame({cv::Point(76, 48)}),
    };
    const auto reverseResults = service.processBatch(reverseFrames, config);
    const auto reverseValid = validFrames(reverseResults);
    if (reverseValid.size() != 2)
    {
        std::cerr << "expected chronological right-to-left motion to start a new track, got "
                  << reverseValid.size() << " valid tracks from "
                  << reverseResults.size() << " total records\n";
        return 5;
    }
    for (size_t i = 0; i < reverseValid.size(); ++i)
    {
        const auto& val = reverseValid[i].validation;
        const int expectedTrackId = static_cast<int>(i + 1);
        const uint64_t expectedFrame = static_cast<uint64_t>(i);
        if (val.trackId != expectedTrackId ||
            val.trackFirstFrame != expectedFrame ||
            val.trackLastFrame != expectedFrame ||
            val.trackObservationCount != 1)
        {
            std::cerr << "bad reverse-motion track metadata for result " << i
                      << ": trackId=" << val.trackId
                      << " first=" << val.trackFirstFrame
                      << " last=" << val.trackLastFrame
                      << " observations=" << val.trackObservationCount << "\n";
            return 6;
        }
    }

    HfStreamEvidence hfEvidence;
    if (!loadAndValidateHfStreamEvidence(service, hfEvidence, std::cerr))
    {
        return 13;
    }

    auto makeReviewSampleWithConfig = [&](std::string id,
                                          std::string caseType,
                                          std::string expected,
                                          const cv::Mat& input,
                                          const backend::services::ProcessingConfig& sampleConfig,
                                          const cv::Mat& background,
                                          const backend::services::ProcessingService::Roi& roi) {
        const auto sampleResults = service.processBatch({input}, sampleConfig, background, roi);
        const auto sampleValid = validFrames(sampleResults);
        ReviewSample sample;
        sample.id = std::move(id);
        sample.caseType = std::move(caseType);
        sample.expected = std::move(expected);
        sample.input = input;
        sample.validDetections = static_cast<int>(sampleValid.size());
        if (!sampleResults.empty())
        {
            sample.processedMask = sampleResults.front().processedImage;
        }
        return sample;
    };

    auto makeReviewSample = [&](std::string id,
                                std::string caseType,
                                std::string expected,
                                const cv::Mat& input) {
        return makeReviewSampleWithConfig(std::move(id), std::move(caseType), std::move(expected),
                                          input, config, cv::Mat{},
                                          backend::services::ProcessingService::Roi{});
    };

    std::vector<ReviewSample> reviewSamples{
        makeReviewSample("sample-001-ltr-start", "normal multi-object",
                         "two left-to-right objects start stable tracks 1 and 2",
                         frames[0]),
        makeReviewSample("sample-002-ltr-reentry", "re-entry after empty frame",
                         "the same two objects re-enter rightward and update existing tracks",
                         frames[3]),
        makeReviewSample("sample-003-empty-gap", "empty/noise frame",
                         "empty gap remains invalid and does not create a track",
                         frames[2]),
        makeReviewSample("sample-004-reverse-start", "reverse-motion start",
                         "first reverse-motion observation creates track 1",
                         reverseFrames[0]),
        makeReviewSample("sample-005-reverse-leftward", "reverse-motion leftward frame",
                         "later leftward observation creates a separate track instead of deduplicating",
                         reverseFrames[1]),
    };
    if (hfEvidence.available)
    {
        cv::Mat background;
        if (!readHfRowImage(std::filesystem::path(std::getenv("KIN9_HF_SAMPLE_DIR")), 24, background, std::cerr))
        {
            return 14;
        }
        const auto hfConfig = hfStreamTrackingConfig();
        const backend::services::ProcessingService::Roi hfRoi{0, 8, 512, 70};
        for (size_t i = 0; i < hfEvidence.inputFrames.size(); ++i)
        {
            const int viewerCell = i < hfEvidence.viewerCells.size()
                                       ? hfEvidence.viewerCells[i]
                                       : static_cast<int>(i + 1);
            std::ostringstream id;
            id << "sample-" << std::setw(3) << std::setfill('0') << (6 + i)
               << "-hf-cell-" << std::setw(3) << std::setfill('0') << viewerCell;
            std::ostringstream expected;
            expected << "HF viewer cell " << viewerCell
                     << " is one observation of the same rightward-moving object; aggregate track 1 spans cells 21-23";
            reviewSamples.push_back(
                makeReviewSampleWithConfig(id.str(), "HF 512x96stream same object",
                                           expected.str(), hfEvidence.inputFrames[i],
                                           hfConfig, background, hfRoi));
        }
    }

    backend::services::Hdf5Service hdf5;
    const std::string path = makeTempPath();
    if (!hdf5.openFile(path))
    {
        std::cerr << "failed to open temporary HDF5 file\n";
        return 7;
    }
    if (!hdf5.appendFrames(valid, invalid))
    {
        std::cerr << "failed to append tracked frames\n";
        hdf5.closeFile();
        return 8;
    }
    hdf5.closeFile();

    if (!hdf5.loadFile(path))
    {
        std::cerr << "failed to reload temporary HDF5 file\n";
        std::filesystem::remove(path);
        return 9;
    }
    std::vector<backend::services::ProcessedFrame> reloaded;
    if (!hdf5.readValidMetadata(reloaded))
    {
        std::cerr << "failed to read valid metadata\n";
        hdf5.closeFile();
        std::filesystem::remove(path);
        return 10;
    }
    hdf5.closeFile();
    std::filesystem::remove(path);

    if (reloaded.size() != valid.size())
    {
        std::cerr << "expected " << valid.size() << " reloaded valid rows, got "
                  << reloaded.size() << "\n";
        return 11;
    }
    for (size_t i = 0; i < reloaded.size(); ++i)
    {
        const auto& val = reloaded[i].validation;
        if (val.trackId != valid[i].validation.trackId ||
            val.trackFirstFrame != valid[i].validation.trackFirstFrame ||
            val.trackLastFrame != valid[i].validation.trackLastFrame ||
            val.trackObservationCount != valid[i].validation.trackObservationCount)
        {
            std::cerr << "tracking metadata did not round-trip for row " << i << "\n";
            return 12;
        }
    }

    if (const char* bundle = std::getenv("KIN9_REVIEW_BUNDLE"))
    {
        writeReviewArtifacts(bundle, frames, valid, reverseFrames, reverseValid, hfEvidence, reviewSamples);
    }

    return 0;
}

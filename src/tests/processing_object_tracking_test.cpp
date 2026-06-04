#include "backend/services/Hdf5Service.h"
#include "backend/services/ProcessingService.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
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

void writeReviewArtifacts(const std::filesystem::path& dir,
                          const std::vector<cv::Mat>& inputFrames,
                          const std::vector<backend::services::ProcessedFrame>& tracks)
{
    std::filesystem::create_directories(dir);

    for (size_t i = 0; i < inputFrames.size(); ++i)
    {
        std::ostringstream name;
        name << "input_frame_" << std::setw(3) << std::setfill('0') << i << ".png";
        cv::imwrite((dir / name.str()).string(), inputFrames[i]);
    }

    if (!tracks.empty())
    {
        cv::imwrite((dir / "processed_mask_track_001.png").string(), tracks.front().processedImage);
    }

    cv::Mat overlayBase;
    cv::cvtColor(inputFrames.front(), overlayBase, cv::COLOR_GRAY2BGR);
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
    cv::imwrite((dir / "tracking_overlay.png").string(), canvas);

    std::ofstream metrics(dir / "metrics.json");
    metrics << "{\n"
            << "  \"input_frames\": " << inputFrames.size() << ",\n"
            << "  \"raw_valid_observations\": 6,\n"
            << "  \"deduped_valid_tracks\": " << tracks.size() << ",\n"
            << "  \"duplicate_detections_suppressed\": " << (6 - tracks.size()) << ",\n"
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
    metrics << "  ]\n"
            << "}\n";

    std::ofstream manifest(dir / "README.md");
    manifest << "# KIN-9 Review Bundle\n\n"
             << "- `input_frame_*.png`: synthetic HF-stream-style frame sequence with two moving ring objects and one empty gap frame.\n"
             << "- `processed_mask_track_001.png`: processed mask generated by `ProcessingService` for the first retained track record.\n"
             << "- `tracking_overlay.png`: retained track records with stable IDs and first/last frame spans overlaid on the first input frame.\n"
             << "- `metrics.json`: raw observation count, deduplicated track count, suppressed duplicate count, and per-track spans.\n\n"
             << "Regenerate visuals with `KIN9_REVIEW_BUNDLE=review_artifacts/KIN-9 ./build/linux-backend/mib_processing_object_tracking_test`.\n"
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

    backend::services::Hdf5Service hdf5;
    const std::string path = makeTempPath();
    if (!hdf5.openFile(path))
    {
        std::cerr << "failed to open temporary HDF5 file\n";
        return 5;
    }
    if (!hdf5.appendFrames(valid, invalid))
    {
        std::cerr << "failed to append tracked frames\n";
        hdf5.closeFile();
        return 6;
    }
    hdf5.closeFile();

    if (!hdf5.loadFile(path))
    {
        std::cerr << "failed to reload temporary HDF5 file\n";
        std::filesystem::remove(path);
        return 7;
    }
    std::vector<backend::services::ProcessedFrame> reloaded;
    if (!hdf5.readValidMetadata(reloaded))
    {
        std::cerr << "failed to read valid metadata\n";
        hdf5.closeFile();
        std::filesystem::remove(path);
        return 8;
    }
    hdf5.closeFile();
    std::filesystem::remove(path);

    if (reloaded.size() != valid.size())
    {
        std::cerr << "expected " << valid.size() << " reloaded valid rows, got "
                  << reloaded.size() << "\n";
        return 9;
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
            return 10;
        }
    }

    if (const char* bundle = std::getenv("KIN9_REVIEW_BUNDLE"))
    {
        writeReviewArtifacts(bundle, frames, valid);
    }

    return 0;
}

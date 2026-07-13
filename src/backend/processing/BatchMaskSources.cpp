#include "backend/processing/BatchMaskSources.h"

#include "backend/recording/Hdf5Service.h"
#include "backend/processing/ProcessingService.h"

#include <spdlog/spdlog.h>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <iomanip>

namespace fs = std::filesystem;

namespace backend::services::batch_masks {

bool loadFromHdf5(Hdf5Service& hdf5,
                  const std::string& datasetPath,
                  size_t startIndex,
                  size_t count,
                  std::vector<cv::Mat>& outGray) {
    outGray.clear();
    std::vector<cv::Mat> imgs;
    if (!hdf5.readImagesRange(datasetPath, startIndex, count, imgs)) {
        SPDLOG_ERROR("loadFromHdf5: readImagesRange failed ({} [{}..{}))",
                     datasetPath, startIndex, startIndex + count);
        return false;
    }
    outGray.reserve(imgs.size());
    for (auto& m : imgs) {
        if (m.empty()) { outGray.push_back(cv::Mat()); continue; }
        if (m.type() == CV_8UC1) {
            outGray.push_back(std::move(m));
        } else if (m.channels() == 3) {
            cv::Mat g;
            cv::cvtColor(m, g, cv::COLOR_BGR2GRAY);
            outGray.push_back(std::move(g));
        } else {
            cv::Mat g;
            m.convertTo(g, CV_8UC1);
            outGray.push_back(std::move(g));
        }
    }
    SPDLOG_INFO("loadFromHdf5: loaded {} images from {} starting at {}",
                outGray.size(), datasetPath, startIndex);
    return true;
}

static bool isSupportedImageExt(const std::string& ext) {
    std::string e = ext;
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return std::tolower(c); });
    return e == ".tif" || e == ".tiff" || e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".bmp";
}

bool loadFromFolder(const std::string& folderPath,
                    std::vector<cv::Mat>& outGray,
                    std::vector<std::string>& outFilenames,
                    std::vector<std::string>& errors) {
    outGray.clear();
    outFilenames.clear();
    errors.clear();

    std::error_code ec;
    if (!fs::exists(folderPath, ec) || !fs::is_directory(folderPath, ec)) {
        errors.push_back("Not a directory: " + folderPath);
        SPDLOG_ERROR("loadFromFolder: not a directory: {}", folderPath);
        return false;
    }

    std::vector<fs::path> paths;
    for (auto& entry : fs::directory_iterator(folderPath, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (!isSupportedImageExt(entry.path().extension().string())) continue;
        paths.push_back(entry.path());
    }
    std::sort(paths.begin(), paths.end(),
              [](const fs::path& a, const fs::path& b) { return a.filename() < b.filename(); });

    outGray.reserve(paths.size());
    outFilenames.reserve(paths.size());
    for (const auto& p : paths) {
        cv::Mat m = cv::imread(p.string(), cv::IMREAD_GRAYSCALE);
        if (m.empty()) {
            errors.push_back("Failed to read: " + p.string());
            SPDLOG_WARN("loadFromFolder: failed to read {}", p.string());
            continue;
        }
        outGray.push_back(std::move(m));
        outFilenames.push_back(p.filename().string());
    }
    SPDLOG_INFO("loadFromFolder: loaded {} images from {} ({} errors)",
                outGray.size(), folderPath, errors.size());
    return true;
}

bool loadFromAvi(const std::string& aviPath,
                 std::vector<cv::Mat>& outGray,
                 std::vector<std::string>& outFilenames,
                 std::vector<std::string>& errors) {
    outGray.clear();
    outFilenames.clear();
    errors.clear();

    std::error_code ec;
    if (!fs::exists(aviPath, ec) || !fs::is_regular_file(aviPath, ec)) {
        errors.push_back("Not a file: " + aviPath);
        SPDLOG_ERROR("loadFromAvi: not a file: {}", aviPath);
        return false;
    }

    cv::VideoCapture cap(aviPath);
    if (!cap.isOpened()) {
        errors.push_back("Failed to open AVI: " + aviPath);
        SPDLOG_ERROR("loadFromAvi: failed to open {}", aviPath);
        return false;
    }

    const int hintedCount = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));
    if (hintedCount > 0) {
        outGray.reserve(static_cast<size_t>(hintedCount));
        outFilenames.reserve(static_cast<size_t>(hintedCount));
    }

    size_t idx = 0;
    cv::Mat frame;
    while (true) {
        if (!cap.read(frame) || frame.empty()) break;

        cv::Mat gray;
        if (frame.channels() == 1) {
            gray = frame.clone();
            if (gray.type() != CV_8UC1) {
                cv::Mat tmp;
                gray.convertTo(tmp, CV_8UC1);
                gray = std::move(tmp);
            }
        } else if (frame.channels() == 3) {
            cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
        } else if (frame.channels() == 4) {
            cv::cvtColor(frame, gray, cv::COLOR_BGRA2GRAY);
        } else {
            std::ostringstream oss;
            oss << "Unsupported channel count (" << frame.channels()
                << ") at frame " << idx;
            errors.push_back(oss.str());
            SPDLOG_WARN("loadFromAvi: {}", oss.str());
            ++idx;
            continue;
        }

        std::ostringstream oss;
        oss << "frame_" << std::setw(5) << std::setfill('0') << idx;
        outGray.push_back(std::move(gray));
        outFilenames.push_back(oss.str());
        ++idx;
    }

    cap.release();

    if (outGray.empty()) {
        errors.push_back("No frames decoded from: " + aviPath);
        SPDLOG_WARN("loadFromAvi: decoded 0 frames from {}", aviPath);
        return false;
    }

    SPDLOG_INFO("loadFromAvi: loaded {} frames from {} ({} errors)",
                outGray.size(), aviPath, errors.size());
    return true;
}

size_t saveMaskImages(const std::vector<ProcessedFrame>& frames,
                      const std::string& outputDir,
                      const std::vector<std::string>& filenames) {
    if (frames.empty()) return 0;

    std::error_code ec;
    fs::create_directories(outputDir, ec);
    if (ec) {
        SPDLOG_ERROR("saveMaskImages: cannot create directory {}: {}", outputDir, ec.message());
        return 0;
    }

    size_t written = 0;
    for (size_t i = 0; i < frames.size(); ++i) {
        const cv::Mat& mask = frames[i].processedImage;
        if (mask.empty()) continue;

        std::string name;
        if (i < filenames.size() && !filenames[i].empty()) {
            fs::path p(filenames[i]);
            name = p.stem().string() + "_mask.png";
        } else {
            std::ostringstream oss;
            oss << "mask_" << std::setw(5) << std::setfill('0') << i << ".png";
            name = oss.str();
        }
        const fs::path out = fs::path(outputDir) / name;
        if (cv::imwrite(out.string(), mask)) {
            ++written;
        } else {
            SPDLOG_WARN("saveMaskImages: imwrite failed for {}", out.string());
        }
    }
    SPDLOG_INFO("saveMaskImages: wrote {} masks to {}", written, outputDir);
    return written;
}

bool saveMasksToHdf5(const std::vector<ProcessedFrame>& frames,
                     const std::string& outputPath,
                     const ProcessingConfig& config,
                     int roiX, int roiY, int roiW, int roiH,
                     const cv::Mat& background,
                     bool useFrameTimestamps,
                     const backend::processing::ProcessingCoreIdentity* processingCore) {
    if (frames.empty()) {
        SPDLOG_WARN("saveMasksToHdf5: no frames to save");
        return false;
    }

    Hdf5Service out;
    if (!out.openFile(outputPath)) {
        SPDLOG_ERROR("saveMasksToHdf5: cannot open {}", outputPath);
        return false;
    }

    std::vector<ProcessedFrame> valid, invalid;
    valid.reserve(frames.size());
    invalid.reserve(frames.size());
    for (const auto& f : frames) {
        if (f.validation.isValid) valid.push_back(f);
        else invalid.push_back(f);
    }

    const auto nowNs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    uint64_t startTimeNs = nowNs;
    uint64_t endTimeNs = nowNs;
    if (useFrameTimestamps) {
        startTimeNs = 0;
        endTimeNs = 0;
        for (const auto& frame : frames) {
            endTimeNs = std::max(endTimeNs, frame.timestampNs);
        }
    }

    ProcessingService::Roi roi{roiX, roiY, roiW, roiH};
    const cv::Mat* bgPtr = background.empty() ? nullptr : &background;

    if (!out.writeExperimentInfo(startTimeNs, endTimeNs, valid.size(), invalid.size(), config, roi,
                                 bgPtr, processingCore)) {
        SPDLOG_ERROR("saveMasksToHdf5: writeExperimentInfo failed");
        out.closeFile();
        return false;
    }

    if (!out.saveFrames(valid, invalid)) {
        SPDLOG_ERROR("saveMasksToHdf5: saveFrames failed");
        out.closeFile();
        return false;
    }

    out.closeFile();
    SPDLOG_INFO("saveMasksToHdf5: wrote {} valid + {} invalid frames to {}",
                valid.size(), invalid.size(), outputPath);
    return true;
}

} // namespace backend::services::batch_masks

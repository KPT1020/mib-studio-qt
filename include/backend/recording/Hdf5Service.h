#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <functional>
#include <string_view>

namespace cv {
    class Mat;
}

namespace backend::services {
    struct ProcessedFrame;
    struct ProcessingConfig;
}

#include "backend/processing/ProcessingService.h"

namespace backend::services {

// Optional telemetry hook for HDF5 I/O performance events (file close,
// frame append). No-op until set, so Hdf5Service carries no CrashReporter
// (and therefore no Qt) dependency -- it lives in the Qt-free mib_processing
// target. The desktop app wires this to
// CrashReporter::capturePerformanceTransaction during startup
// (src/frontend/core/main.cpp); tests and portable consumers may leave it
// unset or set their own sink.
using PerformanceTraceFn = std::function<void(std::string_view name,
                                              std::string_view operation,
                                              double durationMs,
                                              std::string_view jsonData)>;
void setHdf5PerformanceTraceHook(PerformanceTraceFn fn);

class Hdf5Service {
public:
    Hdf5Service();
    ~Hdf5Service();

    bool initialize(const std::string& rootDir);
    
    // File operations
    bool openFile(const std::string& filePath);
    bool loadFile(const std::string& filePath); // Open existing file for reading
    void closeFile();
    bool flush(); // Explicit global flush — call before metadata writes to protect frame data on crash
    bool isFileOpen() const;
    
    // Frame saving (batch write - for final save or periodic flush)
    bool saveFrames(const std::vector<ProcessedFrame>& validFrames,
                    const std::vector<ProcessedFrame>& invalidFrames);
    
    // Incremental frame appending (for round-robin buffer resilience)
    bool initializeDatasets(); // Create datasets with unlimited dimensions
    bool appendFrames(const std::vector<ProcessedFrame>& validFrames,
                      const std::vector<ProcessedFrame>& invalidFrames);
    
    // Frame reading
    bool readValidFrames(std::vector<ProcessedFrame>& frames);
    bool readInvalidFrames(std::vector<ProcessedFrame>& frames);
    
    // Experiment metadata
    bool writeExperimentInfo(uint64_t startTimeNs, uint64_t endTimeNs,
                             size_t totalValidFrames, size_t totalInvalidFrames,
                             const ProcessingConfig& processingConfig,
                             const ProcessingService::Roi& roi,
                             const cv::Mat* background = nullptr);
    bool readExperimentInfo(uint64_t& startTimeNs, uint64_t& endTimeNs,
                             size_t& totalValidFrames, size_t& totalInvalidFrames,
                             ProcessingService::Roi* roi = nullptr);

    // Save raw config JSON as a string attribute on /experiment_info.
    // Precondition: writeExperimentInfo() must have been called first.
    bool writeConfigJson(const std::string& jsonContent);

    // Read background image saved for the run (if present). Returns false if not open, dataset missing, or read fails.
    bool readBackgroundImage(cv::Mat& out) const;

    // Scalable read APIs for review (lazy, on-demand)
    // Retrieve dataset shape information. Returns false if dataset missing or file not open.
    bool getDatasetInfo(const std::string& datasetPath,
                        size_t& outCount,
                        int& outHeight,
                        int& outWidth,
                        int& outChannels) const;

    // Read a single image at index using hyperslab selection (bounded memory).
    // Supports both 3D (N,H,W) and 4D (N,H,W,C) datasets; outputs CV_8UC1 or CV_8UC(C).
    bool readImageByIndex(const std::string& datasetPath,
                          size_t index,
                          cv::Mat& outImage) const;

    // Read a small range of images [startIndex, startIndex+count) using iterative hyperslabs.
    // Designed for small batches (e.g., thumbnails). Returns false if any read fails.
    bool readImagesRange(const std::string& datasetPath,
                         size_t startIndex,
                         size_t count,
                         std::vector<cv::Mat>& outImages) const;

    // Metadata-only reads (do not load image/mask payloads)
    bool readValidMetadata(std::vector<ProcessedFrame>& frames);
    bool readInvalidMetadata(std::vector<ProcessedFrame>& frames);

    // Chart snapshot saving (for saving chart images to HDF5)
    bool saveChartSnapshot(const std::string& datasetPath, const cv::Mat& image);
    
    // Chart snapshot reading (for reading 2D/3D chart images without batch dimension)
    bool readChartSnapshot(const std::string& datasetPath, cv::Mat& outImage) const;

    // --- Multi-image series support ---
    // Read the series_images 4D dataset shape: (N, seriesCount, H, W)
    bool getSeriesImageInfo(size_t& outCount, size_t& outSeriesCount,
                            int& outHeight, int& outWidth) const;

    // Read a single series record at index (returns seriesCount images)
    bool readSeriesImagesByIndex(size_t index, std::vector<cv::Mat>& outImages) const;

    // --- Frame recording mode (images + basic metadata, no contour processing) ---

    // Simple metadata for frame recording (no contour metrics)
    struct RecordingFrameMeta {
        uint64_t index{0};
        uint64_t timestampNs{0};
        uint64_t width{0};
        uint64_t height{0};
    };

    // Initialize recording datasets (creates /recorded_frames group)
    bool initializeRecordingDatasets();

    // Append raw frames for recording mode (images + basic metadata only)
    bool appendRecordingFrames(const std::vector<cv::Mat>& images,
                               const std::vector<RecordingFrameMeta>& metadata);

    // Write recording info attributes
    bool writeRecordingInfo(uint64_t startTimeNs, uint64_t endTimeNs,
                            uint64_t totalFrames, uint64_t filteredFrames,
                            bool multiImageEnabled = false,
                            uint64_t multiImageCount = 1);

    // Recording-mode readers (counterparts to the write* functions above).
    // isRecordingFile() detects a recording-mode file via the presence of
    // /recording_info. readRecordingMetadata fills only index + timestampNs
    // on each ProcessedFrame; other fields remain default.
    bool isRecordingFile() const;
    bool readRecordingMetadata(std::vector<ProcessedFrame>& frames);
    bool readRecordingInfo(uint64_t& startTimeNs, uint64_t& endTimeNs,
                           uint64_t& totalFrames, uint64_t& filteredFrames,
                           bool* multiImageEnabled = nullptr,
                           uint64_t* multiImageCount = nullptr);

private:
    // Flush at most once per configured interval (env MIB_HDF5_FLUSH_INTERVAL_MS,
    // default 5 s). Used by append hot paths so the recorder thread avoids
    // synchronous I/O on every batch; closeFile() still does a final flush.
    bool maybeIntervalFlush();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace backend::services

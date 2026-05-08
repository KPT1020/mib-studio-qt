#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

#include <opencv2/core.hpp>

#include "backend/services/Hdf5Service.h"

using backend::services::Hdf5Service;
using backend::services::ProcessedFrame;

namespace {
struct TempFile
{
    std::filesystem::path path;
    ~TempFile()
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

static std::filesystem::path uniqueTmpPath(const std::string& prefix, const std::string& ext)
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    auto p = std::filesystem::temp_directory_path() / (prefix + std::to_string(now) + ext);
    return p;
}
}

TEST_CASE("Hdf5Service recording-mode readers round-trip metadata and info", "[Hdf5Service]")
{
    TempFile tmp{uniqueTmpPath("mib_recording_", ".h5")};

    Hdf5Service h;
    REQUIRE(h.openFile(tmp.path.string()));
    REQUIRE(h.initializeRecordingDatasets());

    std::vector<cv::Mat> images;
    images.emplace_back(4, 4, CV_8UC1, cv::Scalar(0));
    images.emplace_back(4, 4, CV_8UC1, cv::Scalar(0));
    images[0].at<uint8_t>(1, 1) = 200;
    images[1].at<uint8_t>(2, 2) = 210;

    std::vector<Hdf5Service::RecordingFrameMeta> meta;
    meta.push_back(Hdf5Service::RecordingFrameMeta{10, 1000, 4, 4});
    meta.push_back(Hdf5Service::RecordingFrameMeta{11, 2000, 4, 4});

    REQUIRE(h.appendRecordingFrames(images, meta));
    REQUIRE(h.writeRecordingInfo(1000, 2000, 2, 0));
    h.closeFile();

    REQUIRE(h.loadFile(tmp.path.string()));
    REQUIRE(h.isRecordingFile());

    std::vector<ProcessedFrame> frames;
    REQUIRE(h.readRecordingMetadata(frames));
    REQUIRE(frames.size() == 2);
    CHECK(frames[0].index == 10);
    CHECK(frames[0].timestampNs == 1000);
    CHECK(frames[0].originalImage.empty());
    CHECK(frames[0].processedImage.empty());

    CHECK(frames[1].index == 11);
    CHECK(frames[1].timestampNs == 2000);

    uint64_t start = 0;
    uint64_t end = 0;
    uint64_t total = 0;
    uint64_t filtered = 0;
    REQUIRE(h.readRecordingInfo(start, end, total, filtered));
    CHECK(start == 1000);
    CHECK(end == 2000);
    CHECK(total == 2);
    CHECK(filtered == 0);
}

TEST_CASE("Hdf5Service recording-mode readers fail on non-recording files", "[Hdf5Service]")
{
    TempFile tmp{uniqueTmpPath("mib_non_recording_", ".h5")};

    Hdf5Service h;
    REQUIRE(h.openFile(tmp.path.string()));
    h.closeFile();

    REQUIRE(h.loadFile(tmp.path.string()));
    CHECK_FALSE(h.isRecordingFile());

    uint64_t start = 0;
    uint64_t end = 0;
    uint64_t total = 0;
    uint64_t filtered = 0;
    CHECK_FALSE(h.readRecordingInfo(start, end, total, filtered));
}

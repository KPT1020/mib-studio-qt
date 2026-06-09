#include "backend/recording/Hdf5Service.h"
#include "backend/recording/RecorderService.h"

#include <opencv2/core.hpp>

#include <filesystem>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace
{
std::filesystem::path makeTempPath()
{
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<unsigned long long> dist;
    return std::filesystem::temp_directory_path() /
           ("mib_recording_lifecycle_" + std::to_string(dist(gen)) + ".h5");
}

cv::Mat makeFrame(unsigned char value)
{
    return cv::Mat(8, 10, CV_8UC1, cv::Scalar(value));
}
} // namespace

int main()
{
    backend::services::RecorderService recorder;
    if (recorder.isOpen() || recorder.openForWrite(std::filesystem::temp_directory_path().string()))
    {
        std::cerr << "RecorderService should remain closed when recorder support is disabled\n";
        return 1;
    }
    const unsigned char pixel = 1;
    if (recorder.writeFrame(&pixel, 1, 1, 1, 1, 0, 1, 1, 1) || recorder.isOpen())
    {
        std::cerr << "disabled RecorderService should reject frame writes and stay closed\n";
        return 2;
    }

    const auto path = makeTempPath();
    backend::services::Hdf5Service hdf5;
    if (!hdf5.openFile(path.string()))
    {
        std::cerr << "failed to open HDF5 recording file\n";
        return 3;
    }
    if (!hdf5.initializeRecordingDatasets())
    {
        std::cerr << "failed to initialize recording datasets\n";
        hdf5.closeFile();
        std::filesystem::remove(path);
        return 4;
    }

    std::vector<cv::Mat> firstBatch{makeFrame(30), makeFrame(60)};
    std::vector<backend::services::Hdf5Service::RecordingFrameMeta> firstMeta{
        {0, 1000, 10, 8},
        {1, 2000, 10, 8},
    };
    if (!hdf5.appendRecordingFrames(firstBatch, firstMeta))
    {
        std::cerr << "failed to append first recording batch\n";
        hdf5.closeFile();
        std::filesystem::remove(path);
        return 5;
    }

    std::vector<cv::Mat> secondBatch{makeFrame(90)};
    std::vector<backend::services::Hdf5Service::RecordingFrameMeta> secondMeta{
        {2, 3000, 10, 8},
    };
    if (!hdf5.appendRecordingFrames(secondBatch, secondMeta))
    {
        std::cerr << "failed to append second recording batch\n";
        hdf5.closeFile();
        std::filesystem::remove(path);
        return 6;
    }
    if (!hdf5.writeRecordingInfo(1000, 4000, 3, 1))
    {
        std::cerr << "failed to write recording info\n";
        hdf5.closeFile();
        std::filesystem::remove(path);
        return 7;
    }
    hdf5.closeFile();

    if (!hdf5.loadFile(path.string()))
    {
        std::cerr << "failed to reload HDF5 recording file\n";
        std::filesystem::remove(path);
        return 8;
    }
    if (!hdf5.isRecordingFile())
    {
        std::cerr << "recording file marker should be present\n";
        hdf5.closeFile();
        std::filesystem::remove(path);
        return 9;
    }

    uint64_t start = 0;
    uint64_t end = 0;
    uint64_t total = 0;
    uint64_t filtered = 0;
    if (!hdf5.readRecordingInfo(start, end, total, filtered) ||
        start != 1000 || end != 4000 || total != 3 || filtered != 1)
    {
        std::cerr << "recording info should round-trip\n";
        hdf5.closeFile();
        std::filesystem::remove(path);
        return 10;
    }

    std::vector<backend::services::ProcessedFrame> metadata;
    if (!hdf5.readRecordingMetadata(metadata) || metadata.size() != 3)
    {
        std::cerr << "recording metadata should contain three appended frames\n";
        hdf5.closeFile();
        std::filesystem::remove(path);
        return 11;
    }
    for (size_t i = 0; i < metadata.size(); ++i)
    {
        if (metadata[i].index != i || metadata[i].timestampNs != (i + 1) * 1000)
        {
            std::cerr << "bad metadata row " << i << ": index=" << metadata[i].index
                      << " timestamp=" << metadata[i].timestampNs << "\n";
            hdf5.closeFile();
            std::filesystem::remove(path);
            return 12;
        }
    }

    size_t count = 0;
    int height = 0;
    int width = 0;
    int channels = 0;
    if (!hdf5.getDatasetInfo("/recorded_frames/images", count, height, width, channels) ||
        count != 3 || height != 8 || width != 10 || channels != 1)
    {
        std::cerr << "recorded image dataset shape should match appended frames\n";
        hdf5.closeFile();
        std::filesystem::remove(path);
        return 13;
    }

    hdf5.closeFile();
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".recovery.h5");
    return 0;
}

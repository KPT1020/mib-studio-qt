#include "backend/recording/Hdf5Service.h"

#include <opencv2/core.hpp>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace
{
std::filesystem::path makeTempPath()
{
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<unsigned long long> dist;
    return std::filesystem::temp_directory_path() /
           ("mib_hdf5_checkpoint_throttle_" + std::to_string(dist(gen)) + ".h5");
}

backend::services::ProcessedFrame makeSeriesFrame(uint64_t index, uint8_t seed)
{
    backend::services::ProcessedFrame frame;
    frame.index = index;
    frame.timestampNs = index * 1000;
    frame.originalImage = cv::Mat(64, 64, CV_8UC1, cv::Scalar(seed)).clone();
    frame.processedImage = cv::Mat(64, 64, CV_8UC1, cv::Scalar(0)).clone();
    frame.seriesImages.reserve(15);
    for (int i = 0; i < 15; ++i)
    {
        frame.seriesImages.push_back(cv::Mat(64, 64, CV_8UC1, cv::Scalar(static_cast<uint8_t>(seed + i + 1))).clone());
    }
    return frame;
}
} // namespace

int main()
{
    const auto path = makeTempPath();
    const auto recoveryPath = path.string() + ".recovery.h5";

    backend::services::Hdf5Service hdf5;
    if (!hdf5.openFile(path.string()))
    {
        std::cerr << "failed to open temporary HDF5 file\n";
        return 1;
    }
    if (!hdf5.initializeDatasets())
    {
        std::cerr << "failed to initialize datasets\n";
        hdf5.closeFile();
        std::filesystem::remove(path);
        return 2;
    }

    std::vector<backend::services::ProcessedFrame> firstBatch;
    std::vector<backend::services::ProcessedFrame> secondBatch;
    for (uint64_t i = 0; i < 6; ++i)
    {
        firstBatch.push_back(makeSeriesFrame(i, static_cast<uint8_t>(20 + i)));
        secondBatch.push_back(makeSeriesFrame(i + 6, static_cast<uint8_t>(40 + i)));
    }

    if (!hdf5.appendFrames(firstBatch, {}))
    {
        std::cerr << "failed to append first multi-image batch\n";
        hdf5.closeFile();
        std::filesystem::remove(path);
        return 3;
    }
    if (!std::filesystem::exists(recoveryPath))
    {
        std::cerr << "recovery checkpoint should exist after first append\n";
        hdf5.closeFile();
        std::filesystem::remove(path);
        return 4;
    }
    const auto recoverySizeAfterFirst = std::filesystem::file_size(recoveryPath);
    const auto recoveryTimeBeforeSecond = std::filesystem::last_write_time(recoveryPath);

    if (!hdf5.appendFrames(secondBatch, {}))
    {
        std::cerr << "failed to append second multi-image batch\n";
        hdf5.closeFile();
        std::filesystem::remove(path);
        std::filesystem::remove(recoveryPath);
        return 5;
    }

    const auto recoverySizeAfterSecond = std::filesystem::file_size(recoveryPath);
    const auto recoveryTimeAfterSecond = std::filesystem::last_write_time(recoveryPath);
    if (recoverySizeAfterSecond != recoverySizeAfterFirst ||
        recoveryTimeAfterSecond != recoveryTimeBeforeSecond)
    {
        std::cerr << "second append should not rewrite recovery checkpoint immediately\n";
        hdf5.closeFile();
        std::filesystem::remove(path);
        std::filesystem::remove(recoveryPath);
        return 6;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    backend::services::ProcessingConfig processingConfig;
    backend::services::ProcessingService::Roi roi{};
    if (!hdf5.writeExperimentInfo(1000, 2000, 12, 0, processingConfig, roi, nullptr))
    {
        std::cerr << "failed to write experiment metadata\n";
        hdf5.closeFile();
        std::filesystem::remove(path);
        std::filesystem::remove(recoveryPath);
        return 7;
    }
    if (!hdf5.writeConfigJson("{\"checkpoint\":\"forced\"}"))
    {
        std::cerr << "failed to write config metadata\n";
        hdf5.closeFile();
        std::filesystem::remove(path);
        std::filesystem::remove(recoveryPath);
        return 8;
    }

    const auto recoveryTimeAfterMetadata = std::filesystem::last_write_time(recoveryPath);
    if (recoveryTimeAfterMetadata <= recoveryTimeAfterSecond)
    {
        std::cerr << "metadata writes should force a checkpoint refresh\n";
        hdf5.closeFile();
        std::filesystem::remove(path);
        std::filesystem::remove(recoveryPath);
        return 9;
    }

    hdf5.closeFile();
    std::filesystem::remove(path);
    std::filesystem::remove(recoveryPath);
    return 0;
}

#include "backend/recording/Hdf5Service.h"

#include <opencv2/core.hpp>

#include <chrono>
#include <cstdlib>
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
           ("mib_hdf5_save_performance_" + std::to_string(dist(gen)) + ".h5");
}

void cleanup(const std::filesystem::path& path)
{
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

cv::Mat makeFrame(unsigned char value)
{
    cv::Mat image(128, 128, CV_8UC1);
    image.setTo(cv::Scalar(value));
    return image;
}

double maxAllowedMs()
{
    const char* env = std::getenv("MIB_HDF5_PERF_MAX_MS");
    if (!env || !*env)
    {
        return 30000.0;
    }

    char* end = nullptr;
    const double parsed = std::strtod(env, &end);
    return (end != env && parsed > 0.0) ? parsed : 30000.0;
}
} // namespace

int main()
{
    // Smoke guard over repeated recording appends. Appends now flush at most
    // once per MIB_HDF5_FLUSH_INTERVAL_MS (no per-batch full-file copy), so this
    // should stay well under the limit; data integrity is checked after the
    // final flush + strong close on reload below.
    constexpr size_t batchCount = 20;
    constexpr size_t batchSize = 10;
    constexpr size_t expectedFrames = batchCount * batchSize;

    const auto path = makeTempPath();
    cleanup(path);

    backend::services::Hdf5Service hdf5;
    if (!hdf5.openFile(path.string()))
    {
        std::cerr << "failed to open performance test HDF5 file\n";
        cleanup(path);
        return 1;
    }
    if (!hdf5.initializeRecordingDatasets())
    {
        std::cerr << "failed to initialize performance test recording datasets\n";
        hdf5.closeFile();
        cleanup(path);
        return 2;
    }

    const auto start = std::chrono::steady_clock::now();
    uint64_t frameIndex = 0;
    for (size_t batch = 0; batch < batchCount; ++batch)
    {
        std::vector<cv::Mat> images;
        std::vector<backend::services::Hdf5Service::RecordingFrameMeta> metadata;
        images.reserve(batchSize);
        metadata.reserve(batchSize);

        for (size_t i = 0; i < batchSize; ++i)
        {
            const auto value = static_cast<unsigned char>((frameIndex % 251) + 1);
            images.push_back(makeFrame(value));
            metadata.push_back({frameIndex, (frameIndex + 1) * 1000, 128, 128});
            ++frameIndex;
        }

        if (!hdf5.appendRecordingFrames(images, metadata))
        {
            std::cerr << "failed to append performance batch " << batch << "\n";
            hdf5.closeFile();
            cleanup(path);
            return 3;
        }
    }

    if (!hdf5.writeRecordingInfo(1000, (expectedFrames + 1) * 1000, expectedFrames, 0))
    {
        std::cerr << "failed to write performance test recording info\n";
        hdf5.closeFile();
        cleanup(path);
        return 4;
    }
    hdf5.closeFile();

    const double elapsedMs = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - start)
                                 .count();
    if (elapsedMs > maxAllowedMs())
    {
        std::cerr << "HDF5 recording save performance smoke exceeded limit: "
                  << elapsedMs << " ms for " << expectedFrames << " frames\n";
        cleanup(path);
        return 5;
    }

    if (!hdf5.loadFile(path.string()))
    {
        std::cerr << "failed to reload performance test HDF5 file\n";
        cleanup(path);
        return 6;
    }

    size_t count = 0;
    int height = 0;
    int width = 0;
    int channels = 0;
    if (!hdf5.getDatasetInfo("/recorded_frames/images", count, height, width, channels) ||
        count != expectedFrames || height != 128 || width != 128 || channels != 1)
    {
        std::cerr << "performance test dataset shape mismatch\n";
        hdf5.closeFile();
        cleanup(path);
        return 7;
    }

    for (size_t index : {size_t{0}, expectedFrames / 2, expectedFrames - 1})
    {
        cv::Mat image;
        const auto expected = static_cast<unsigned char>((index % 251) + 1);
        if (!hdf5.readImageByIndex("/recorded_frames/images", index, image) ||
            image.empty() || image.at<unsigned char>(0, 0) != expected ||
            image.at<unsigned char>(127, 127) != expected)
        {
            std::cerr << "performance test payload mismatch at index " << index << "\n";
            hdf5.closeFile();
            cleanup(path);
            return 8;
        }
    }

    hdf5.closeFile();
    cleanup(path);
    return 0;
}

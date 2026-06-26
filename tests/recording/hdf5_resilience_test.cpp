#include "backend/recording/Hdf5Service.h"

#include <opencv2/core.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace
{
struct RecordingFixture
{
    std::filesystem::path path;
    std::vector<unsigned char> values;
};

std::filesystem::path makeTempPath(const std::string& stem)
{
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<unsigned long long> dist;
    return std::filesystem::temp_directory_path() /
           (stem + "_" + std::to_string(dist(gen)) + ".h5");
}

cv::Mat makeFrame(unsigned char value)
{
    return cv::Mat(8, 10, CV_8UC1, cv::Scalar(value));
}

backend::services::ProcessedFrame makeProcessedFrame(uint64_t index, unsigned char value, bool valid)
{
    backend::services::ProcessedFrame frame;
    frame.index = index;
    frame.timestampNs = (index + 1) * 1000;
    frame.originalImage = makeFrame(value);
    frame.processedImage = makeFrame(valid ? 255 : 0);
    frame.validation.isValid = valid;
    frame.validation.deformability = valid ? 0.25 : 0.0;
    frame.validation.area = valid ? 120.0 : 10.0;
    frame.validation.areaRatio = 1.0;
    frame.validation.ringRatio = 20.0;
    frame.validation.inRange = valid;
    return frame;
}

void cleanup(const std::filesystem::path& path)
{
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

bool writeRecording(const RecordingFixture& fixture, bool explicitClose)
{
    backend::services::Hdf5Service hdf5;
    if (!hdf5.openFile(fixture.path.string()))
    {
        std::cerr << "failed to open HDF5 recording file\n";
        return false;
    }
    if (!hdf5.initializeRecordingDatasets())
    {
        std::cerr << "failed to initialize recording datasets\n";
        hdf5.closeFile();
        return false;
    }

    std::vector<cv::Mat> firstBatch{makeFrame(fixture.values[0]), makeFrame(fixture.values[1])};
    std::vector<backend::services::Hdf5Service::RecordingFrameMeta> firstMeta{
        {0, 1000, 10, 8},
        {1, 2000, 10, 8},
    };
    if (!hdf5.appendRecordingFrames(firstBatch, firstMeta))
    {
        std::cerr << "failed to append first recording batch\n";
        hdf5.closeFile();
        return false;
    }

    std::vector<cv::Mat> secondBatch{makeFrame(fixture.values[2])};
    std::vector<backend::services::Hdf5Service::RecordingFrameMeta> secondMeta{
        {2, 3000, 10, 8},
    };
    if (!hdf5.appendRecordingFrames(secondBatch, secondMeta))
    {
        std::cerr << "failed to append second recording batch\n";
        hdf5.closeFile();
        return false;
    }

    if (!hdf5.writeRecordingInfo(1000, 4000, 3, 1))
    {
        std::cerr << "failed to write recording info\n";
        hdf5.closeFile();
        return false;
    }

    if (explicitClose)
    {
        hdf5.closeFile();
    }

    return true;
}

bool verifyRecording(const RecordingFixture& fixture)
{
    backend::services::Hdf5Service hdf5;
    if (!hdf5.loadFile(fixture.path.string()))
    {
        std::cerr << "failed to load recording file\n";
        return false;
    }

    if (!hdf5.isRecordingFile())
    {
        std::cerr << "recording marker missing\n";
        hdf5.closeFile();
        return false;
    }

    uint64_t start = 0;
    uint64_t end = 0;
    uint64_t total = 0;
    uint64_t filtered = 0;
    if (!hdf5.readRecordingInfo(start, end, total, filtered) ||
        start != 1000 || end != 4000 || total != 3 || filtered != 1)
    {
        std::cerr << "recording info not preserved\n";
        hdf5.closeFile();
        return false;
    }

    std::vector<backend::services::ProcessedFrame> metadata;
    if (!hdf5.readRecordingMetadata(metadata) || metadata.size() != fixture.values.size())
    {
        std::cerr << "recording metadata not preserved\n";
        hdf5.closeFile();
        return false;
    }
    for (size_t i = 0; i < metadata.size(); ++i)
    {
        if (metadata[i].index != i || metadata[i].timestampNs != (i + 1) * 1000)
        {
            std::cerr << "bad metadata row " << i << "\n";
            hdf5.closeFile();
            return false;
        }
    }

    size_t count = 0;
    int height = 0;
    int width = 0;
    int channels = 0;
    if (!hdf5.getDatasetInfo("/recorded_frames/images", count, height, width, channels) ||
        count != fixture.values.size() || height != 8 || width != 10 || channels != 1)
    {
        std::cerr << "recorded image dataset shape not preserved\n";
        hdf5.closeFile();
        return false;
    }

    for (size_t i = 0; i < fixture.values.size(); ++i)
    {
        cv::Mat image;
        if (!hdf5.readImageByIndex("/recorded_frames/images", i, image) ||
            image.empty() || image.at<unsigned char>(0, 0) != fixture.values[i] ||
            image.at<unsigned char>(7, 9) != fixture.values[i])
        {
            std::cerr << "recorded image payload not preserved at index " << i << "\n";
            hdf5.closeFile();
            return false;
        }
    }

    hdf5.closeFile();
    return true;
}

bool corruptPrimaryFile(const std::filesystem::path& path)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        std::cerr << "failed to open primary file for corruption\n";
        return false;
    }
    out << "not an hdf5 file";
    return out.good();
}

bool writeExperiment(const std::filesystem::path& path, bool explicitClose)
{
    backend::services::Hdf5Service hdf5;
    if (!hdf5.openFile(path.string()))
    {
        std::cerr << "failed to open HDF5 experiment file\n";
        return false;
    }
    if (!hdf5.initializeDatasets())
    {
        std::cerr << "failed to initialize experiment datasets\n";
        hdf5.closeFile();
        return false;
    }

    std::vector<backend::services::ProcessedFrame> valid{
        makeProcessedFrame(0, 101, true),
        makeProcessedFrame(1, 102, true),
    };
    std::vector<backend::services::ProcessedFrame> invalid{
        makeProcessedFrame(2, 201, false),
    };
    if (!hdf5.appendFrames(valid, invalid))
    {
        std::cerr << "failed to append experiment frames\n";
        hdf5.closeFile();
        return false;
    }

    backend::services::ProcessingConfig config;
    backend::services::ProcessingService::Roi roi{1, 2, 3, 4};
    if (!hdf5.writeExperimentInfo(1000, 5000, valid.size(), invalid.size(), config, roi))
    {
        std::cerr << "failed to write experiment info\n";
        hdf5.closeFile();
        return false;
    }

    if (explicitClose)
    {
        hdf5.closeFile();
    }
    return true;
}

bool verifyExperiment(const std::filesystem::path& path)
{
    backend::services::Hdf5Service hdf5;
    if (!hdf5.loadFile(path.string()))
    {
        std::cerr << "failed to load experiment file\n";
        return false;
    }

    uint64_t start = 0;
    uint64_t end = 0;
    size_t validTotal = 0;
    size_t invalidTotal = 0;
    backend::services::ProcessingService::Roi roi;
    if (!hdf5.readExperimentInfo(start, end, validTotal, invalidTotal, &roi) ||
        start != 1000 || end != 5000 || validTotal != 2 || invalidTotal != 1 ||
        roi.x != 1 || roi.y != 2 || roi.w != 3 || roi.h != 4)
    {
        std::cerr << "experiment info not preserved\n";
        hdf5.closeFile();
        return false;
    }

    size_t count = 0;
    int height = 0;
    int width = 0;
    int channels = 0;
    if (!hdf5.getDatasetInfo("/valid_frames/images", count, height, width, channels) ||
        count != 2 || height != 8 || width != 10 || channels != 1)
    {
        std::cerr << "valid experiment image shape not preserved\n";
        hdf5.closeFile();
        return false;
    }
    if (!hdf5.getDatasetInfo("/invalid_frames/images", count, height, width, channels) ||
        count != 1 || height != 8 || width != 10 || channels != 1)
    {
        std::cerr << "invalid experiment image shape not preserved\n";
        hdf5.closeFile();
        return false;
    }

    const std::vector<unsigned char> validValues{101, 102};
    for (size_t i = 0; i < validValues.size(); ++i)
    {
        cv::Mat image;
        if (!hdf5.readImageByIndex("/valid_frames/images", i, image) ||
            image.empty() || image.at<unsigned char>(0, 0) != validValues[i] ||
            image.at<unsigned char>(7, 9) != validValues[i])
        {
            std::cerr << "valid experiment image payload not preserved at index " << i << "\n";
            hdf5.closeFile();
            return false;
        }
    }

    cv::Mat invalidImage;
    if (!hdf5.readImageByIndex("/invalid_frames/images", 0, invalidImage) ||
        invalidImage.empty() || invalidImage.at<unsigned char>(0, 0) != 201 ||
        invalidImage.at<unsigned char>(7, 9) != 201)
    {
        std::cerr << "invalid experiment image payload not preserved\n";
        hdf5.closeFile();
        return false;
    }

    hdf5.closeFile();
    return true;
}

// Strong close (H5F_CLOSE_STRONG) plus the destructor's final flush must
// finalize a writable file even when the caller never calls closeFile().
bool testDestructorFinalizesWritableFile()
{
    RecordingFixture fixture{makeTempPath("mib_hdf5_forced_close"), {11, 22, 33}};
    cleanup(fixture.path);
    if (!writeRecording(fixture, false))
    {
        cleanup(fixture.path);
        return false;
    }

    const bool ok = verifyRecording(fixture);
    cleanup(fixture.path);
    return ok;
}

// With no recovery checkpoint, a corrupt primary must simply fail to load.
bool testCorruptedPrimaryFailsCleanly()
{
    RecordingFixture fixture{makeTempPath("mib_hdf5_corrupt_primary"), {77, 88, 99}};
    cleanup(fixture.path);
    if (!writeRecording(fixture, true))
    {
        cleanup(fixture.path);
        return false;
    }

    // No .recovery.h5 should ever be produced.
    if (std::filesystem::exists(fixture.path.string() + ".recovery.h5"))
    {
        std::cerr << "unexpected recovery checkpoint produced\n";
        cleanup(fixture.path);
        return false;
    }

    if (!corruptPrimaryFile(fixture.path))
    {
        cleanup(fixture.path);
        return false;
    }

    backend::services::Hdf5Service hdf5;
    const bool loaded = hdf5.loadFile(fixture.path.string());
    hdf5.closeFile();
    cleanup(fixture.path);
    if (loaded)
    {
        std::cerr << "corrupted primary should not load\n";
        return false;
    }
    return true;
}

// Experiment files finalize the same way via strong close on destruction.
bool testExperimentForcedClosePreservesData()
{
    const auto path = makeTempPath("mib_hdf5_experiment_forced_close");
    cleanup(path);
    if (!writeExperiment(path, false))
    {
        cleanup(path);
        return false;
    }

    const bool ok = verifyExperiment(path);
    cleanup(path);
    return ok;
}
} // namespace

int main()
{
    if (!testDestructorFinalizesWritableFile())
    {
        return 1;
    }
    if (!testCorruptedPrimaryFailsCleanly())
    {
        return 2;
    }
    if (!testExperimentForcedClosePreservesData())
    {
        return 3;
    }
    return 0;
}

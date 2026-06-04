#include "backend/services/ProcessingService.h"
#include "backend/services/Hdf5Service.h"

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <random>
#include <vector>

namespace
{
cv::Mat makeTwoRingObjects()
{
    cv::Mat image(96, 160, CV_8UC1, cv::Scalar(0));
    const std::vector<cv::Point> centers{{48, 48}, {112, 48}};
    for (const auto& center : centers)
    {
        cv::circle(image, center, 22, cv::Scalar(255), cv::FILLED);
        cv::circle(image, center, 11, cv::Scalar(0), cv::FILLED);
    }
    return image;
}

backend::services::ProcessingConfig permissiveRingConfig()
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
            ("mib_processing_multi_object_" + std::to_string(dist(gen)) + ".h5"))
        .string();
}
} // namespace

int main()
{
    backend::services::ProcessingService service;
    const cv::Mat image = makeTwoRingObjects();
    const auto config = permissiveRingConfig();

    const auto results = service.processBatch({image}, config);
    if (results.size() != 2)
    {
        std::cerr << "expected 2 object records from one frame, got " << results.size() << "\n";
        if (!results.empty())
        {
            std::cerr << "first record: valid=" << results[0].validation.isValid
                      << " innerContourCount=" << results[0].validation.innerContourCount
                      << " area=" << results[0].validation.area
                      << " ringRatio=" << results[0].validation.ringRatio
                      << " deformability=" << results[0].validation.deformability << "\n";
        }
        return 1;
    }

    for (size_t i = 0; i < results.size(); ++i)
    {
        const auto& frame = results[i];
        const auto& validation = frame.validation;
        if (frame.index != 0)
        {
            std::cerr << "record " << i << " should preserve source frame index 0, got "
                      << frame.index << "\n";
            return 2;
        }
        if (validation.objectId != static_cast<int>(i) || validation.objectCount != 2)
        {
            std::cerr << "record " << i << " has object metadata objectId="
                      << validation.objectId << " objectCount=" << validation.objectCount << "\n";
            return 3;
        }
        if (!validation.isValid)
        {
            std::cerr << "record " << i << " should be valid\n";
            return 4;
        }
        if (validation.area <= 0.0 || validation.ringRatio <= 0.0 ||
            !std::isfinite(validation.deformability))
        {
            std::cerr << "record " << i << " missing per-object metrics: area="
                      << validation.area << " ringRatio=" << validation.ringRatio
                      << " deformability=" << validation.deformability << "\n";
            return 5;
        }
    }

    backend::services::Hdf5Service hdf5;
    const std::string path = makeTempPath();
    if (!hdf5.openFile(path))
    {
        std::cerr << "failed to open temporary HDF5 file\n";
        return 6;
    }
    if (!hdf5.appendFrames(results, {}))
    {
        std::cerr << "failed to create per-object frames through append path\n";
        hdf5.closeFile();
        return 7;
    }
    if (!hdf5.appendFrames(results, {}))
    {
        std::cerr << "failed to append per-object frames\n";
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

    if (reloaded.size() != 4)
    {
        std::cerr << "expected 4 reloaded metadata rows after append, got " << reloaded.size() << "\n";
        return 11;
    }
    for (size_t i = 0; i < reloaded.size(); ++i)
    {
        const auto& validation = reloaded[i].validation;
        const int expectedObjectId = static_cast<int>(i % 2);
        if (reloaded[i].index != 0 ||
            validation.objectId != expectedObjectId ||
            validation.objectCount != 2 ||
            validation.area <= 0.0 ||
            validation.ringRatio <= 0.0)
        {
            std::cerr << "bad reloaded object row " << i << ": index=" << reloaded[i].index
                      << " objectId=" << validation.objectId
                      << " objectCount=" << validation.objectCount
                      << " area=" << validation.area
                      << " ringRatio=" << validation.ringRatio << "\n";
            return 12;
        }
    }

    return 0;
}

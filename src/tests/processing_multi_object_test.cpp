#include "backend/services/ProcessingService.h"

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <iostream>
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
        if (!validation.isValid)
        {
            std::cerr << "record " << i << " should be valid\n";
            return 3;
        }
        if (validation.area <= 0.0 || validation.ringRatio <= 0.0 ||
            !std::isfinite(validation.deformability))
        {
            std::cerr << "record " << i << " missing per-object metrics: area="
                      << validation.area << " ringRatio=" << validation.ringRatio
                      << " deformability=" << validation.deformability << "\n";
            return 4;
        }
    }

    return 0;
}

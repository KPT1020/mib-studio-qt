#include "backend/processing/ProcessingService.h"

#include <opencv2/imgproc.hpp>

#include <iostream>
#include <vector>

namespace
{
cv::Mat makeRingFrame()
{
    cv::Mat image(80, 80, CV_8UC1, cv::Scalar(0));
    cv::circle(image, cv::Point(40, 40), 20, cv::Scalar(255), cv::FILLED);
    cv::circle(image, cv::Point(40, 40), 8, cv::Scalar(0), cv::FILLED);
    return image;
}

backend::services::ProcessingConfig makeSmokeConfig()
{
    backend::services::ProcessingConfig config;
    config.gaussian_blur_size = 1;
    config.bg_subtract_threshold = 127;
    config.morph_kernel_size = 1;
    config.morph_iterations = 1;
    config.enable_area_range_check = false;
    config.enable_deformability_range_check = false;
    config.enable_ring_ratio_check = false;
    config.enable_area_ratio_check = false;
    config.enable_border_check = true;
    config.require_single_inner_contour = true;
    config.empty_frame_pixel_threshold = 1;
    return config;
}
} // namespace

int main()
{
    backend::services::ProcessingService service;
    const auto config = makeSmokeConfig();
    const auto frame = makeRingFrame();

    const auto processed = service.computeProcessedFrame(
        frame, cv::Mat{}, config, backend::services::ProcessingService::Roi{0, 0, 0, 0}, 42, 4200);
    if (processed.index != 42 || processed.timestampNs != 4200)
    {
        std::cerr << "computeProcessedFrame should preserve index and timestamp\n";
        return 1;
    }
    if (processed.originalImage.empty() || processed.processedImage.empty())
    {
        std::cerr << "computeProcessedFrame should emit original image and mask\n";
        return 2;
    }
    if (!processed.validation.isValid || processed.validation.area <= 0.0 ||
        processed.validation.ringRatio <= 0.0)
    {
        std::cerr << "processing smoke frame should produce a valid ring object\n";
        return 3;
    }

    int progressCalls = 0;
    const auto batch = service.processBatch({frame, frame}, config, cv::Mat{},
                                            backend::services::ProcessingService::Roi{0, 0, 0, 0},
                                            [&](const backend::services::ProcessingService::BatchProgress& progress) {
                                                ++progressCalls;
                                                if (progress.done > progress.total)
                                                {
                                                    std::cerr << "progress done exceeded total\n";
                                                }
                                            });
    if (batch.size() != 1)
    {
        std::cerr << "processBatch should dedupe repeated smoke observations to one track, got "
                  << batch.size() << "\n";
        return 4;
    }
    if (batch.front().validation.trackFirstFrame != 0 ||
        batch.front().validation.trackLastFrame != 1 ||
        batch.front().validation.trackObservationCount != 2)
    {
        std::cerr << "processBatch should preserve repeated observation track metadata\n";
        return 5;
    }
    if (progressCalls == 0)
    {
        std::cerr << "processBatch should invoke progress callback\n";
        return 6;
    }

    return 0;
}

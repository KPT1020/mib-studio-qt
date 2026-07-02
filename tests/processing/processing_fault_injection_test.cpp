// Fault-injection: exceptions thrown inside ProcessingService's threads
// (worker jobs, batch workers, batch result callbacks) must be contained —
// logged and dropped — never allowed to escape the thread entry function,
// which would std::terminate the whole process.

#include "backend/processing/ProcessingService.h"

#include <opencv2/imgproc.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
cv::Mat makeRingFrame()
{
    cv::Mat image(80, 80, CV_8UC1, cv::Scalar(0));
    cv::circle(image, cv::Point(40, 40), 20, cv::Scalar(255), cv::FILLED);
    cv::circle(image, cv::Point(40, 40), 8, cv::Scalar(0), cv::FILLED);
    return image;
}

bool waitFor(const std::function<bool()>& condition, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (condition())
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return condition();
}
} // namespace

int main()
{
    using namespace std::chrono_literals;

    // 1) A submitted job that throws must not kill the worker pool.
    {
        backend::services::ProcessingService service;
        service.start(2);

        service.submit([] { throw std::runtime_error("injected job failure"); });
        service.submit([] { throw 42; }); // non-std exception path

        std::atomic<bool> goodJobRan{false};
        service.submit([&goodJobRan] { goodJobRan.store(true); });

        if (!waitFor([&] { return goodJobRan.load(); }, 5s))
        {
            std::cerr << "worker pool should survive throwing jobs and run later jobs\n";
            return 1;
        }
        service.stop();
    }

    // 2) A batch result callback that throws must not kill the batch workers;
    //    later batches must still be delivered.
    {
        backend::services::ProcessingService service;

        std::atomic<int> batchesDelivered{0};
        backend::services::ProcessingService::BatchPipelineConfig config;
        config.batchSize = 1;
        config.maxQueuedFrames = 32;
        config.workerCount = 1;
        config.maxBatchDelayMs = 5;
        config.processing.gaussian_blur_size = 1;
        config.processing.morph_kernel_size = 1;
        config.processing.morph_iterations = 1;
        config.processing.bg_subtract_threshold = 127;
        config.processing.empty_frame_pixel_threshold = 1;

        const bool started = service.startBatchPipeline(
            config,
            [&batchesDelivered](std::vector<backend::services::ProcessedFrame>) {
                const int delivered = batchesDelivered.fetch_add(1) + 1;
                if (delivered == 1)
                {
                    throw std::runtime_error("injected callback failure");
                }
            });
        if (!started)
        {
            std::cerr << "batch pipeline should start\n";
            return 2;
        }

        const cv::Mat frame = makeRingFrame();
        for (uint64_t i = 0; i < 8; ++i)
        {
            service.enqueueBatchFrame(frame, i, i * 100);
            std::this_thread::sleep_for(10ms);
        }

        if (!waitFor([&] { return batchesDelivered.load() >= 2; }, 5s))
        {
            std::cerr << "batch workers should survive a throwing result callback; delivered="
                      << batchesDelivered.load() << "\n";
            return 3;
        }
        service.stopBatchPipeline();
    }

    std::cout << "processing fault-injection test passed\n";
    return 0;
}

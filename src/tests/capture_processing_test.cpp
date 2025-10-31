#include "backend/AppBackend.h"
#include "backend/services/CaptureService.h"
#include "backend/services/ProcessingService.h"

#include <chrono>
#include <thread>
#include <spdlog/spdlog.h>

int main() {
    try {
        backend::AppBackend app;
        if (!app.initialize("data")) {
            SPDLOG_ERROR("Backend initialize failed");
            return 1;
        }

        auto& cap = app.capture();
        SPDLOG_INFO("Starting capture for 2 seconds...");
        cap.start();

        std::this_thread::sleep_for(std::chrono::seconds(2));

        cap.stop();

        const auto& cstats = cap.stats();
        SPDLOG_INFO("Frames processed: {} | Last fps: {} | MB/s: {}",
                    cstats.framesProcessed.load(),
                    cstats.lastFrameRate.load(),
                    cstats.lastDataRateMBps.load());

        const auto& pstats = app.processing().stats();
        SPDLOG_INFO("CPU jobs queued: {} | processed: {}",
                    pstats.jobsQueued.load(), pstats.jobsProcessed.load());

        return 0;
    } catch (const std::exception& ex) {
        SPDLOG_ERROR("Test exception: {}", ex.what());
        return 2;
    }
}

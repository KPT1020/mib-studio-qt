#include "backend/AppBackend.h"
#include "backend/services/CaptureService.h"
#include "backend/services/ProcessingService.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>
#include <spdlog/spdlog.h>

namespace {

#ifdef _WIN32
void setEnv(const char* key, const std::string& value) {
    _putenv_s(key, value.c_str());
}
#else
void setEnv(const char* key, const std::string& value) {
    setenv(key, value.c_str(), 1);
}
#endif

void ensureMockFrames(const std::filesystem::path& dir) {
    std::filesystem::create_directories(dir);
    const auto framePath = dir / "frame_000.png";
    if (std::filesystem::exists(framePath)) {
        return;
    }

    static const unsigned char kSinglePixelPng[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
        0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53,
        0xDE, 0x00, 0x00, 0x00, 0x0A, 0x49, 0x44, 0x41,
        0x54, 0x08, 0xD7, 0x63, 0xF8, 0x0F, 0x04, 0x00,
        0x09, 0xFB, 0x03, 0xFD, 0xBF, 0x18, 0x81, 0x00,
        0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE,
        0x42, 0x60, 0x82
    };

    std::ofstream out(framePath, std::ios::binary);
    out.write(reinterpret_cast<const char*>(kSinglePixelPng), sizeof(kSinglePixelPng));
    out.flush();
}

} // namespace

int main() {
    try {
        const std::filesystem::path mockDir = std::filesystem::path("data") / "mock_frames";
        ensureMockFrames(mockDir);
        setEnv("MIB_CAMERA_MODE", "mock");
        setEnv("MIB_MOCK_CAMERA_DIR", mockDir.string());
        setEnv("MIB_MOCK_CAMERA_LOOP", "false");

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

// e2e_pipeline_stress_test
//
// Hunts the user complaint: "random crashes." Crashes in this app most
// plausibly come from lifecycle races in the capture/recording threading:
// starting/stopping capture, toggling frame recording, and the camera-ready
// callback handing a raw camera pointer across threads. This test drives the
// real AppBackend (CaptureService + FrameStore + MockCamera + Hdf5 recording)
// through many start/stop cycles, including deliberately tight race windows,
// and asserts the process survives without crashing or hanging and that
// recording actually produced data.
//
// Usage: e2e_pipeline_stress_test [frameDir] [cycles]
//   frameDir : directory of images for the mock camera (defaults to synthetic)
//   cycles   : number of lifecycle cycles (default 40)
//
// Point frameDir at real data (e.g. C:\Users\gavin\Developer\data\10um\1) to
// stress with production-sized frames.

#include "backend/app/AppBackend.h"
#include "backend/services/CaptureService.h"
#include "backend/camera/mock/MockCamera.h"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <QCoreApplication>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <random>
#include <string>
#include <thread>

namespace fs = std::filesystem;

namespace {

std::string randomTag()
{
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<unsigned long long> dist;
    return std::to_string(dist(gen));
}

bool waitFor(const std::function<bool()>& pred, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return pred();
}

bool generateSyntheticFrames(const fs::path& dir, int count)
{
    std::error_code ec;
    fs::create_directories(dir, ec);
    for (int i = 0; i < count; ++i) {
        cv::Mat img(256, 256, CV_8UC1, cv::Scalar(0));
        cv::circle(img, cv::Point(80 + i % 64, 128), 30, cv::Scalar(255), -1);
        cv::circle(img, cv::Point(80 + i % 64, 128), 14, cv::Scalar(0), -1);
        char name[32];
        std::snprintf(name, sizeof(name), "frame_%05d.png", i);
        if (!cv::imwrite((dir / name).string(), img)) return false;
    }
    return true;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    fs::path frameDir;
    // Light default so CI finishes well under the ctest timeout even on a slow
    // runner; spinning capture up/down and reloading mock frames per cycle is
    // the cost. Pass a larger [cycles] arg for heavy local stress runs.
    int cycles = 8;
    bool synthesized = false;

    const fs::path scratch = fs::temp_directory_path() /
                             ("mib_e2e_stress_" + randomTag());
    std::error_code ec;
    fs::create_directories(scratch, ec);

    if (argc >= 2) {
        frameDir = argv[1];
    } else {
        frameDir = scratch / "frames";
        if (!generateSyntheticFrames(frameDir, 48)) {
            std::cerr << "failed to synthesize frames\n";
            return 2;
        }
        synthesized = true;
    }
    if (argc >= 3) cycles = std::max(1, std::stoi(argv[2]));

    if (!fs::exists(frameDir)) {
        std::cerr << "frame directory does not exist: " << frameDir << "\n";
        return 3;
    }

    const fs::path recDir = scratch / "recordings";
    fs::create_directories(recDir, ec);

    backend::AppBackend backend;
    if (!backend.initialize((scratch / "data").string())) {
        std::cerr << "AppBackend init failed\n";
        return 4;
    }

    camera::mock::MockCameraOptions opts;
    opts.folder = frameDir;
    opts.frameInterval = std::chrono::microseconds(200);  // ~5 kHz; keep pipeline busy
    opts.loopFiles = true;  // never run out during the stress loop
    backend.configureMockCamera(opts);

    uint64_t totalRecorded = 0;
    int recordingsWithData = 0;

    std::cout << "=== e2e pipeline lifecycle stress ===\n";
    std::cout << "frameDir=" << frameDir.string()
              << (synthesized ? " (synthetic)" : " (provided)")
              << "  cycles=" << cycles << "\n\n";

    // Phase 1: normal start -> record -> stop cycles.
    for (int i = 0; i < cycles; ++i) {
        if (!backend.capture().start()) {
            std::cerr << "cycle " << i << ": capture start failed\n";
            return 5;
        }
        if (!waitFor([&] { return backend.capture().isRunning(); },
                     std::chrono::seconds(2))) {
            std::cerr << "cycle " << i << ": capture never reported running\n";
            return 6;
        }
        // Let some frames flow into the FrameStore.
        waitFor([&] { return backend.capture().stats().framesProcessed.load() > 0; },
                std::chrono::seconds(2));

        const fs::path recPath = recDir / ("rec_" + std::to_string(i) + ".h5");
        const bool recStarted = backend.startFrameRecording(recPath.string());
        if (recStarted) {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            backend.stopFrameRecording();
            const uint64_t n = backend.frameRecordingCount();
            totalRecorded += n;
            if (n > 0) ++recordingsWithData;
        }

        backend.capture().stop();
        if (!waitFor([&] { return !backend.capture().isRunning(); },
                     std::chrono::seconds(2))) {
            std::cerr << "cycle " << i << ": capture failed to stop (possible hang)\n";
            return 7;
        }
    }

    // Phase 2: tight start/stop races (no settle time) to provoke lifecycle
    // data races in the capture thread and camera-ready callback.
    const int raceCycles = cycles * 3;
    for (int i = 0; i < raceCycles; ++i) {
        backend.capture().start();
        if (i % 3 == 0) {
            const fs::path recPath = recDir / ("race_" + std::to_string(i) + ".h5");
            backend.startFrameRecording(recPath.string());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        backend.stopFrameRecording();
        backend.capture().stop();
    }
    // Must come to rest.
    backend.capture().stop();
    if (!waitFor([&] { return !backend.capture().isRunning(); },
                 std::chrono::seconds(3))) {
        std::cerr << "phase 2: pipeline did not come to rest (possible hang)\n";
        return 8;
    }

    std::cout << "completed " << cycles << " normal + " << raceCycles
              << " race cycles without crashing or hanging.\n";
    std::cout << "recordings with data: " << recordingsWithData << "/" << cycles
              << "   total frames recorded: " << totalRecorded << "\n";

    if (synthesized) {
        fs::remove_all(scratch, ec);
    } else {
        fs::remove_all(recDir, ec);
        fs::remove_all(scratch / "data", ec);
    }

    // Surviving all cycles is the primary success signal. If recording never
    // captured a single frame across all normal cycles, the recording path is
    // broken (a different, reportable defect).
    if (recordingsWithData == 0) {
        std::cout << "\nWARNING: no frame recording produced data across all "
                     "normal cycles; recording path may be broken.\n";
        return 9;
    }

    std::cout << "\nPASS: pipeline survived lifecycle stress.\n";
    return 0;
}

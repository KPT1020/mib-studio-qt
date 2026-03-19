// Visual capture test for MindVision camera.
//
// Applies two different configs (overview 1920x1080 vs experiment 512x96),
// grabs frames, and saves them as PNG images for visual verification.
//
// Build & run:
//   cmake --build build --preset windows-default-build --target mindvision_visual_test
//   ctest --test-dir build -R mindvision_visual --output-on-failure

#ifdef _WIN32
#include <windows.h>
#include <stdio.h>
#endif
#include "MindVision/CameraApiLoad.h"

#include "camera/common/MindVisionCamera.h"

#include <QCoreApplication>

#include <opencv2/imgcodecs.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Minimal test harness (copied from mindvision_config_test.cpp)
// ---------------------------------------------------------------------------

struct TestResult {
    std::string name;
    bool        passed  = false;
    bool        skipped = false;
    std::string detail;
};

static std::vector<TestResult> g_results;
static std::vector<std::string> g_savedImages;

#define BEGIN_TEST(name_str)                                         \
    [&]() -> TestResult {                                            \
        const char* _test_name = name_str;                          \
        TestResult  _r;                                              \
        _r.name = _test_name;                                        \
        SPDLOG_INFO("[ RUN  ] {}", _test_name);

#define END_TEST                                                      \
        _r.passed = true;                                             \
        SPDLOG_INFO("[  OK  ] {}", _test_name);                      \
        return _r;                                                    \
    }()

#define SKIP_TEST(reason)                                            \
    do {                                                             \
        _r.skipped = true;                                           \
        _r.passed  = true;                                           \
        _r.detail  = reason;                                         \
        SPDLOG_WARN("[ SKIP ] {} – {}", _test_name, reason);        \
        return _r;                                                   \
    } while (0)

#define EXPECT_TRUE(cond)                                            \
    do {                                                             \
        if (!(cond)) {                                               \
            _r.detail = "EXPECT_TRUE(" #cond ") failed at line "     \
                        + std::to_string(__LINE__);                  \
            SPDLOG_ERROR("[ FAIL ] {} – {}", _test_name, _r.detail); \
            return _r;                                               \
        }                                                            \
    } while (0)

#define EXPECT_EQ(a, b)                                              \
    do {                                                             \
        if ((a) != (b)) {                                            \
            _r.detail = "EXPECT_EQ(" #a ", " #b ") failed: "        \
                        + std::to_string(a) + " != "                 \
                        + std::to_string(b)                          \
                        + " at line " + std::to_string(__LINE__);    \
            SPDLOG_ERROR("[ FAIL ] {} – {}", _test_name, _r.detail); \
            return _r;                                               \
        }                                                            \
    } while (0)

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string makeTimestamp()
{
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &t);
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return ss.str();
}

static bool saveFrameAsPng(const camera::common::Frame& frame,
                           const std::string& path)
{
    cv::Mat mat(static_cast<int>(frame.height),
                static_cast<int>(frame.width),
                CV_8UC1,
                const_cast<uint8_t*>(frame.data.data()),
                frame.linePitch ? frame.linePitch : frame.width);
    return cv::imwrite(path, mat);
}

// Retry grabFrame up to maxRetries times with a delay between attempts.
// Handles transient first-frame errors (e.g. SIZE_DISMATCH after ROI change).
static bool grabFrameWithRetry(camera::common::MindVisionCamera& cam,
                               camera::common::Frame& frame,
                               int maxRetries = 3,
                               int delayMs = 500)
{
    for (int i = 0; i < maxRetries; ++i) {
        if (cam.grabFrame(frame)) return true;
        SPDLOG_WARN("  grabFrame attempt {}/{} failed, retrying in {} ms...",
                     i + 1, maxRetries, delayMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }
    return false;
}

static std::filesystem::path writeTempJson(const char* filename,
                                           const char* content)
{
    auto path = std::filesystem::temp_directory_path() / filename;
    std::ofstream f(path, std::ios::trunc);
    f << content;
    return path;
}

static void removeTempFile(const std::filesystem::path& p)
{
    std::error_code ec;
    std::filesystem::remove(p, ec);
}

// ---------------------------------------------------------------------------
// Test definitions
// ---------------------------------------------------------------------------

static TestResult test_diagnostics()
{
    return BEGIN_TEST("diagnostics: query camera capabilities and packet settings")

        if (LoadSdkApi() != CAMERA_STATUS_SUCCESS) {
            SKIP_TEST("MindVision SDK DLL not available");
        }
        CameraSdkInit(0);

        tSdkCameraDevInfo devList[32];
        INT count = 32;
        if (CameraEnumerateDevice(devList, &count) != CAMERA_STATUS_SUCCESS || count == 0) {
            SKIP_TEST("No MindVision camera found");
        }

        CameraHandle hCam = -1;
        if (CameraInit(&devList[0], -1, -1, &hCam) != CAMERA_STATUS_SUCCESS) {
            SKIP_TEST("CameraInit failed");
        }

        // Query capabilities
        tSdkCameraCapbility cap;
        if (CameraGetCapability(hCam, &cap) == CAMERA_STATUS_SUCCESS) {
            SPDLOG_INFO("  Sensor: {}x{}", cap.sResolutionRange.iWidthMax,
                         cap.sResolutionRange.iHeightMax);
            SPDLOG_INFO("  PackLen options: {}", cap.iPackLenDesc);
            for (int i = 0; i < cap.iPackLenDesc; ++i) {
                SPDLOG_INFO("    [{}] iIndex={} desc={}",
                             i, cap.pPackLenDesc[i].iIndex,
                             cap.pPackLenDesc[i].acDescription);
            }
        }

        // Current packet length
        INT packSel = -1;
        if (CameraGetTransPackLen(hCam, &packSel) == CAMERA_STATUS_SUCCESS) {
            SPDLOG_INFO("  Current TransPackLen index: {}", packSel);
        }

        // Current resolution
        tSdkImageResolution res{};
        if (CameraGetImageResolution(hCam, &res) == CAMERA_STATUS_SUCCESS) {
            SPDLOG_INFO("  Current resolution: {}x{} (FOV: {}x{} @ +{},+{})",
                         res.iWidth, res.iHeight,
                         res.iWidthFOV, res.iHeightFOV,
                         res.iHOffsetFOV, res.iVOffsetFOV);
        }

        CameraUnInit(hCam);

    END_TEST;
}

static TestResult test_overviewCapture()
{
    return BEGIN_TEST("overviewCapture: grab full-sensor frame and save PNG")

        // Use native sensor resolution (816x624) instead of 1920x1080,
        // which exceeds this camera's physical sensor and causes SIZE_DISMATCH.
        const char* json = R"({
  "width": 816,
  "height": 624,
  "offset_x": 0,
  "offset_y": 0,
  "exposure_time_us": 3000.0,
  "trigger_mode": 0,
  "analog_gain": 1
})";
        auto tempPath = writeTempJson("mv_visual_test_overview.json", json);

        camera::common::MindVisionCamera cam(0, tempPath.string());

        if (!cam.start()) {
            removeTempFile(tempPath);
            SKIP_TEST("No MindVision camera hardware available");
        }

        camera::common::Frame frame;
        EXPECT_TRUE(grabFrameWithRetry(cam, frame));
        SPDLOG_INFO("  Grabbed frame: {}x{}, {} bytes",
                     frame.width, frame.height, frame.data.size());

        EXPECT_EQ(frame.width,  uint64_t(816));
        EXPECT_EQ(frame.height, uint64_t(624));

        // Save PNG
        std::filesystem::create_directories("test_output");
        std::string filename = "test_output/mindvision_overview_816x624_"
                             + makeTimestamp() + ".png";
        EXPECT_TRUE(saveFrameAsPng(frame, filename));
        g_savedImages.push_back(std::filesystem::absolute(filename).string());
        SPDLOG_INFO("  Saved: {}", g_savedImages.back());

        cam.stop();
        removeTempFile(tempPath);

    END_TEST;
}

static TestResult test_experimentCapture()
{
    return BEGIN_TEST("experimentCapture: grab 512x96 frame and save PNG")

        // Write temp config with trigger_mode=0 (free-run) so grabFrame
        // doesn't block waiting for a software trigger.
        // Offsets are clamped to fit within the 816x624 sensor.
        const char* json = R"({
  "width": 512,
  "height": 96,
  "offset_x": 152,
  "offset_y": 264,
  "exposure_time_us": 3000.0,
  "trigger_mode": 0,
  "analog_gain": 1
})";
        auto tempPath = writeTempJson("mv_visual_test_experiment.json", json);

        camera::common::MindVisionCamera cam(0, tempPath.string());

        if (!cam.start()) {
            removeTempFile(tempPath);
            SKIP_TEST("No MindVision camera hardware available");
        }

        camera::common::Frame frame;
        EXPECT_TRUE(grabFrameWithRetry(cam, frame));
        SPDLOG_INFO("  Grabbed frame: {}x{}, {} bytes",
                     frame.width, frame.height, frame.data.size());

        EXPECT_EQ(frame.width,  uint64_t(512));
        EXPECT_EQ(frame.height, uint64_t(96));

        // Save PNG
        std::filesystem::create_directories("test_output");
        std::string filename = "test_output/mindvision_experiment_512x96_"
                             + makeTimestamp() + ".png";
        EXPECT_TRUE(saveFrameAsPng(frame, filename));
        g_savedImages.push_back(std::filesystem::absolute(filename).string());
        SPDLOG_INFO("  Saved: {}", g_savedImages.back());

        cam.stop();
        removeTempFile(tempPath);

    END_TEST;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v");

    SPDLOG_INFO("===== MindVision Visual Capture Tests =====");

    g_results.push_back(test_diagnostics());
    g_results.push_back(test_overviewCapture());
    g_results.push_back(test_experimentCapture());

    // Summary
    int passed = 0, failed = 0, skipped = 0;
    SPDLOG_INFO("============================================");
    for (const auto& r : g_results) {
        if (r.skipped) {
            ++skipped;
            SPDLOG_WARN("[ SKIP ] {} – {}", r.name, r.detail);
        } else if (r.passed) {
            ++passed;
            SPDLOG_INFO("[  OK  ] {}", r.name);
        } else {
            ++failed;
            SPDLOG_ERROR("[ FAIL ] {} – {}", r.name, r.detail);
        }
    }
    SPDLOG_INFO("============================================");
    SPDLOG_INFO("Results: {} passed, {} skipped, {} failed", passed, skipped, failed);

    if (!g_savedImages.empty()) {
        SPDLOG_INFO("Saved images:");
        for (const auto& path : g_savedImages) {
            SPDLOG_INFO("  {}", path);
        }
    }

    return (failed == 0) ? 0 : 1;
}

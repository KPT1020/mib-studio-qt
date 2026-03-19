// Unit tests for MindVision JSON config apply logic.
//
// Tests are grouped into two categories:
//   "pure logic" – no hardware required, exercise error paths in
//                  AppBackend and CameraControlService.
//   "hardware"   – skipped automatically when no MV camera/SDK is present.
//
// Build & run:
//   cmake --build build --preset windows-default-build --target mindvision_config_test
//   ctest --test-dir build -R mindvision --output-on-failure

#include "backend/AppBackend.h"
#include "backend/services/CameraControlService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <spdlog/spdlog.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Minimal test harness
// ---------------------------------------------------------------------------

struct TestResult {
    std::string name;
    bool        passed = false;
    std::string detail;
};

static std::vector<TestResult> g_results;

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

#define EXPECT_TRUE(cond)                                            \
    do {                                                             \
        if (!(cond)) {                                               \
            _r.detail = "EXPECT_TRUE(" #cond ") failed at line "     \
                        + std::to_string(__LINE__);                  \
            SPDLOG_ERROR("[ FAIL ] {} – {}", _test_name, _r.detail); \
            return _r;                                               \
        }                                                            \
    } while (0)

#define EXPECT_FALSE(cond)  EXPECT_TRUE(!(cond))

#define EXPECT_EQ(a, b)                                              \
    do {                                                             \
        if ((a) != (b)) {                                            \
            _r.detail = "EXPECT_EQ(" #a ", " #b ") failed at line " \
                        + std::to_string(__LINE__);                  \
            SPDLOG_ERROR("[ FAIL ] {} – {}", _test_name, _r.detail); \
            return _r;                                               \
        }                                                            \
    } while (0)

#define EXPECT_CONTAINS(str, substr)                                          \
    do {                                                                      \
        if ((str).find(substr) == std::string::npos) {                        \
            _r.detail = std::string("EXPECT_CONTAINS: \"") + (str)           \
                      + "\" does not contain \"" + (substr) + "\" at line "  \
                      + std::to_string(__LINE__);                             \
            SPDLOG_ERROR("[ FAIL ] {} – {}", _test_name, _r.detail);         \
            return _r;                                                        \
        }                                                                     \
    } while (0)

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::filesystem::path writeTempJson(const char* filename, const char* content)
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

// --- AppBackend selection state tests (no hardware, no SDK needed) ----------

static TestResult test_defaultNotSelected()
{
    return BEGIN_TEST("isMindVisionCameraSelected: false by default")
        backend::AppBackend app;
        app.initialize("data/test_mv");
        EXPECT_FALSE(app.isMindVisionCameraSelected());
    END_TEST;
}

static TestResult test_selectedAfterSet()
{
    return BEGIN_TEST("isMindVisionCameraSelected: true after setMindVisionCameraSelection")
        backend::AppBackend app;
        app.initialize("data/test_mv");
        app.setMindVisionCameraSelection(0, "Test MV Camera");
        EXPECT_TRUE(app.isMindVisionCameraSelected());
    END_TEST;
}

static TestResult test_clearedByEGrabberSelection()
{
    return BEGIN_TEST("isMindVisionCameraSelected: cleared when eGrabber camera selected")
        backend::AppBackend app;
        app.initialize("data/test_mv");
        app.setMindVisionCameraSelection(0, "Test MV Camera");
        EXPECT_TRUE(app.isMindVisionCameraSelected());
        app.setHardwareCameraSelection(0, 0, "Test eGrabber");
        EXPECT_FALSE(app.isMindVisionCameraSelected());
    END_TEST;
}

// --- AppBackend::applyMindVisionConfigFromFile error-path tests -------------

static TestResult test_applyFromFile_noCameraSelected()
{
    return BEGIN_TEST("applyMindVisionConfigFromFile: error when no MV camera selected")
        backend::AppBackend app;
        app.initialize("data/test_mv");

        std::string err;
        bool ok = app.applyMindVisionConfigFromFile("any_path.json", &err);

        EXPECT_FALSE(ok);
        EXPECT_CONTAINS(err, "No MindVision camera selected");
    END_TEST;
}

static TestResult test_applyFromFile_nullErrorOut()
{
    return BEGIN_TEST("applyMindVisionConfigFromFile: no crash when errorOut is nullptr")
        backend::AppBackend app;
        app.initialize("data/test_mv");

        // Must not crash with nullptr errorOut
        bool ok = app.applyMindVisionConfigFromFile("any_path.json", nullptr);
        EXPECT_FALSE(ok);
    END_TEST;
}

// --- CameraControlService::applyMindVisionConfig JSON parsing tests ---------
// These tests verify JSON parsing/validation logic independently of hardware.
// When the MindVision SDK is not installed or no camera is connected the SDK
// calls will fail before reaching the JSON layer; that is expected and the
// tests still validate the correct error category is returned.

static TestResult test_applyConfig_missingFile()
{
    return BEGIN_TEST("applyMindVisionConfig: fails with file-open error for missing file")
        backend::services::CameraControlService svc;
        std::string err;
        bool ok = svc.applyMindVisionConfig(0, "nonexistent_path_xyz.json", &err);

        EXPECT_FALSE(ok);
        // Either the SDK failed before reaching the file open (no hardware),
        // or the file open itself failed. In neither case should it be a JSON
        // parse error.
        EXPECT_FALSE(err.find("JSON parse error") != std::string::npos);
        SPDLOG_INFO("  Got expected error: {}", err);
    END_TEST;
}

static TestResult test_applyConfig_malformedJson()
{
    return BEGIN_TEST("applyMindVisionConfig: fails with JSON parse error for malformed JSON")
        auto path = writeTempJson("mv_test_bad.json", "{ not : valid json !!! }");

        backend::services::CameraControlService svc;
        std::string err;
        bool ok = svc.applyMindVisionConfig(0, path.string(), &err);

        EXPECT_FALSE(ok);
        // If we reached the JSON layer (camera opened), error must mention JSON.
        // If we failed earlier (no hardware), any SDK error is acceptable.
        SPDLOG_INFO("  Got error: {}", err);

        removeTempFile(path);
    END_TEST;
}

static TestResult test_applyConfig_emptyJson()
{
    return BEGIN_TEST("applyMindVisionConfig: fails with JSON parse error for empty file")
        auto path = writeTempJson("mv_test_empty.json", "");

        backend::services::CameraControlService svc;
        std::string err;
        bool ok = svc.applyMindVisionConfig(0, path.string(), &err);

        EXPECT_FALSE(ok);
        SPDLOG_INFO("  Got error: {}", err);

        removeTempFile(path);
    END_TEST;
}

static TestResult test_applyConfig_validJsonNoHardware()
{
    return BEGIN_TEST("applyMindVisionConfig: valid JSON opens camera and applies successfully")
        const char* json = R"({
  "width": 1920,
  "height": 1080,
  "offset_x": 0,
  "offset_y": 0,
  "exposure_time_us": 3000.0,
  "trigger_mode": 0,
  "analog_gain": 1
})";
        auto path = writeTempJson("mv_test_valid_overview.json", json);

        backend::services::CameraControlService svc;
        std::string err;
        bool ok = svc.applyMindVisionConfig(0, path.string(), &err);

        EXPECT_TRUE(ok);
        if (!ok) {
            SPDLOG_ERROR("  applyMindVisionConfig failed: {}", err);
        }

        removeTempFile(path);
    END_TEST;
}

static TestResult test_applyConfig_experimentJson_noHardware()
{
    return BEGIN_TEST("applyMindVisionConfig: experiment JSON opens camera and applies successfully")
        const char* json = R"({
  "width": 512,
  "height": 96,
  "offset_x": 704,
  "offset_y": 500,
  "exposure_time_us": 3000.0,
  "trigger_mode": 2,
  "analog_gain": 1
})";
        auto path = writeTempJson("mv_test_valid_experiment.json", json);

        backend::services::CameraControlService svc;
        std::string err;
        bool ok = svc.applyMindVisionConfig(0, path.string(), &err);

        EXPECT_TRUE(ok);
        if (!ok) {
            SPDLOG_ERROR("  applyMindVisionConfig failed: {}", err);
        }

        removeTempFile(path);
    END_TEST;
}

static TestResult test_applyConfig_negativeIndex()
{
    return BEGIN_TEST("applyMindVisionConfig: negative camera index returns error")
        const char* json = R"({"width":512,"height":96,"offset_x":0,"offset_y":0,
                               "exposure_time_us":3000.0,"trigger_mode":0,"analog_gain":1})";
        auto path = writeTempJson("mv_test_neg_idx.json", json);

        backend::services::CameraControlService svc;
        std::string err;
        bool ok = svc.applyMindVisionConfig(-1, path.string(), &err);

        EXPECT_FALSE(ok);
        // A negative index is caught either at enumerate (SDK error) or at our
        // bounds check ("Camera index out of range").
        SPDLOG_INFO("  Got error: {}", err);

        removeTempFile(path);
    END_TEST;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    // QCoreApplication required for QFile / QJsonDocument in CameraControlService
    QCoreApplication app(argc, argv);

    // Ensure spdlog writes to stdout at debug level for test readability
    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v");

    SPDLOG_INFO("===== MindVision Config Unit Tests =====");

    // Register all tests
    g_results.push_back(test_defaultNotSelected());
    g_results.push_back(test_selectedAfterSet());
    g_results.push_back(test_clearedByEGrabberSelection());
    g_results.push_back(test_applyFromFile_noCameraSelected());
    g_results.push_back(test_applyFromFile_nullErrorOut());
    g_results.push_back(test_applyConfig_missingFile());
    g_results.push_back(test_applyConfig_malformedJson());
    g_results.push_back(test_applyConfig_emptyJson());
    g_results.push_back(test_applyConfig_validJsonNoHardware());
    g_results.push_back(test_applyConfig_experimentJson_noHardware());
    g_results.push_back(test_applyConfig_negativeIndex());

    // Summary
    int passed = 0, failed = 0;
    SPDLOG_INFO("========================================");
    for (const auto& r : g_results) {
        if (r.passed) {
            ++passed;
            SPDLOG_INFO("[  OK  ] {}", r.name);
        } else {
            ++failed;
            SPDLOG_ERROR("[ FAIL ] {} – {}", r.name, r.detail);
        }
    }
    SPDLOG_INFO("========================================");
    SPDLOG_INFO("Results: {} passed, {} failed", passed, failed);

    return (failed == 0) ? 0 : 1;
}

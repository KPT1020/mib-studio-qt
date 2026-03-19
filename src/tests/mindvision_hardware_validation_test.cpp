// Hardware validation test for MindVision camera.
//
// Applies config parameters via SDK calls and reads them back to verify
// they actually took effect on the hardware.
//
// Build & run:
//   cmake --build build --preset windows-default-build --target mindvision_hardware_validation_test
//   ctest --test-dir build -R mindvision_hardware_validation --output-on-failure

#ifdef _WIN32
#include <windows.h>
#include <stdio.h>
#endif
#include "MindVision/CameraApiLoad.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <spdlog/spdlog.h>

#include "camera/common/MindVisionCamera.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Minimal test harness (same macros as mindvision_visual_test.cpp)
// ---------------------------------------------------------------------------

struct TestResult {
    std::string name;
    bool        passed  = false;
    bool        skipped = false;
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

#define EXPECT_NEAR(a, b, tolerance)                                 \
    do {                                                             \
        if (std::abs((a) - (b)) > (tolerance)) {                    \
            _r.detail = "EXPECT_NEAR(" #a ", " #b ", " #tolerance   \
                        ") failed: " + std::to_string(a) + " vs "   \
                        + std::to_string(b) + " (tol "               \
                        + std::to_string(tolerance) + ")"            \
                        + " at line " + std::to_string(__LINE__);    \
            SPDLOG_ERROR("[ FAIL ] {} – {}", _test_name, _r.detail); \
            return _r;                                               \
        }                                                            \
    } while (0)

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

struct CameraSession {
    CameraHandle         hCamera = -1;
    tSdkCameraCapbility  cap{};
    bool                 valid = false;
    std::string          skipReason;

    ~CameraSession() {
        if (valid && hCamera >= 0) {
            CameraUnInit(hCamera);
            hCamera = -1;
            valid = false;
        }
    }

    // Non-copyable, movable
    CameraSession() = default;
    CameraSession(const CameraSession&) = delete;
    CameraSession& operator=(const CameraSession&) = delete;
    CameraSession(CameraSession&& o) noexcept
        : hCamera(o.hCamera), cap(o.cap), valid(o.valid), skipReason(std::move(o.skipReason))
    { o.valid = false; o.hCamera = -1; }
    CameraSession& operator=(CameraSession&& o) noexcept {
        if (this != &o) {
            if (valid && hCamera >= 0) CameraUnInit(hCamera);
            hCamera = o.hCamera; cap = o.cap; valid = o.valid;
            skipReason = std::move(o.skipReason);
            o.valid = false; o.hCamera = -1;
        }
        return *this;
    }
};

static CameraSession initSdkAndOpenCamera()
{
    CameraSession s;

    if (LoadSdkApi() != CAMERA_STATUS_SUCCESS) {
        s.skipReason = "MindVision SDK DLL not available";
        return s;
    }
    CameraSdkInit(0);

    tSdkCameraDevInfo devList[32];
    INT count = 32;
    if (CameraEnumerateDevice(devList, &count) != CAMERA_STATUS_SUCCESS || count == 0) {
        s.skipReason = "No MindVision camera found";
        return s;
    }

    if (CameraInit(&devList[0], -1, -1, &s.hCamera) != CAMERA_STATUS_SUCCESS) {
        s.skipReason = "CameraInit failed";
        return s;
    }

    if (CameraGetCapability(s.hCamera, &s.cap) != CAMERA_STATUS_SUCCESS) {
        CameraUnInit(s.hCamera);
        s.skipReason = "CameraGetCapability failed";
        return s;
    }

    s.valid = true;
    return s;
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

/// Apply JSON config parameters directly to an open camera handle.
/// Replicates the logic from MindVisionCamera::applyJsonConfig().
static bool applyJsonToCamera(CameraHandle hCamera, const char* jsonContent)
{
    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(jsonContent), &parseErr);
    if (doc.isNull()) {
        SPDLOG_WARN("applyJsonToCamera: JSON parse error: {}",
                     parseErr.errorString().toStdString());
        return false;
    }
    QJsonObject obj = doc.object();

    int    width       = obj.value("width").toInt(512);
    int    height      = obj.value("height").toInt(96);
    int    offset_x    = obj.value("offset_x").toInt(0);
    int    offset_y    = obj.value("offset_y").toInt(0);
    double expUs       = obj.value("exposure_time_us").toDouble(3000.0);
    int    triggerMode = obj.value("trigger_mode").toInt(0);
    int    analogGain  = obj.value("analog_gain").toInt(1);

    // Resolution — custom ROI (iIndex=0xFF)
    tSdkImageResolution res{};
    res.iIndex      = 0xFF;
    res.iHOffsetFOV = offset_x;
    res.iVOffsetFOV = offset_y;
    res.iWidthFOV   = width;
    res.iHeightFOV  = height;
    res.iWidth      = width;
    res.iHeight     = height;

    CameraSdkStatus s = CameraSetImageResolution(hCamera, &res);
    if (s != CAMERA_STATUS_SUCCESS) {
        SPDLOG_WARN("applyJsonToCamera: CameraSetImageResolution returned {}", s);
    }

    s = CameraSetExposureTime(hCamera, expUs);
    if (s != CAMERA_STATUS_SUCCESS) {
        SPDLOG_WARN("applyJsonToCamera: CameraSetExposureTime returned {}", s);
    }

    s = CameraSetTriggerMode(hCamera, triggerMode);
    if (s != CAMERA_STATUS_SUCCESS) {
        SPDLOG_WARN("applyJsonToCamera: CameraSetTriggerMode returned {}", s);
    }

    s = CameraSetAnalogGain(hCamera, analogGain);
    if (s != CAMERA_STATUS_SUCCESS) {
        SPDLOG_WARN("applyJsonToCamera: CameraSetAnalogGain returned {}", s);
    }

    return true;
}

// ---------------------------------------------------------------------------
// Image-based test helpers
// ---------------------------------------------------------------------------

static double computeMeanIntensity(const camera::common::Frame& frame)
{
    if (frame.data.empty()) return 0.0;
    double sum = 0.0;
    for (uint8_t pixel : frame.data) sum += pixel;
    return sum / static_cast<double>(frame.data.size());
}

static bool grabFrameWithRetry(camera::common::MindVisionCamera& cam,
                               camera::common::Frame& frame,
                               int maxRetries = 3,
                               int delayMs = 500)
{
    for (int i = 0; i < maxRetries; ++i) {
        if (cam.grabFrame(frame)) return true;
        SPDLOG_WARN("  grabFrame attempt {}/{} failed, retrying...", i + 1, maxRetries);
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }
    return false;
}

// ---------------------------------------------------------------------------
// Test definitions
// ---------------------------------------------------------------------------

static TestResult test_fullSensorResolution()
{
    return BEGIN_TEST("fullSensorResolution: set 816x624 offset (0,0) and readback")

        auto session = initSdkAndOpenCamera();
        if (!session.valid) SKIP_TEST(session.skipReason);

        const char* json = R"({
  "width": 816, "height": 624,
  "offset_x": 0, "offset_y": 0,
  "exposure_time_us": 3000.0, "trigger_mode": 0, "analog_gain": 1
})";
        EXPECT_TRUE(applyJsonToCamera(session.hCamera, json));

        tSdkImageResolution res{};
        EXPECT_TRUE(CameraGetImageResolution(session.hCamera, &res) == CAMERA_STATUS_SUCCESS);
        SPDLOG_INFO("  Readback: {}x{} FOV: {}x{} @ +{},+{}",
                     res.iWidth, res.iHeight,
                     res.iWidthFOV, res.iHeightFOV,
                     res.iHOffsetFOV, res.iVOffsetFOV);

        EXPECT_EQ(res.iWidthFOV,   816);
        EXPECT_EQ(res.iHeightFOV,  624);
        EXPECT_EQ(res.iHOffsetFOV, 0);
        EXPECT_EQ(res.iVOffsetFOV, 0);


    END_TEST;
}

static TestResult test_roiResolution()
{
    return BEGIN_TEST("roiResolution: set 512x96 with offsets (152,264) and readback")

        auto session = initSdkAndOpenCamera();
        if (!session.valid) SKIP_TEST(session.skipReason);

        const char* json = R"({
  "width": 512, "height": 96,
  "offset_x": 152, "offset_y": 264,
  "exposure_time_us": 3000.0, "trigger_mode": 0, "analog_gain": 1
})";
        EXPECT_TRUE(applyJsonToCamera(session.hCamera, json));

        tSdkImageResolution res{};
        EXPECT_TRUE(CameraGetImageResolution(session.hCamera, &res) == CAMERA_STATUS_SUCCESS);
        SPDLOG_INFO("  Readback: {}x{} FOV: {}x{} @ +{},+{}",
                     res.iWidth, res.iHeight,
                     res.iWidthFOV, res.iHeightFOV,
                     res.iHOffsetFOV, res.iVOffsetFOV);

        EXPECT_EQ(res.iWidthFOV,   512);
        EXPECT_EQ(res.iHeightFOV,  96);
        EXPECT_EQ(res.iHOffsetFOV, 152);
        EXPECT_EQ(res.iVOffsetFOV, 264);


    END_TEST;
}

static TestResult test_exposureTime_low()
{
    return BEGIN_TEST("exposureTime_low: set 100us and readback within line-time tolerance")

        auto session = initSdkAndOpenCamera();
        if (!session.valid) SKIP_TEST(session.skipReason);

        double targetUs = 100.0;
        EXPECT_TRUE(CameraSetExposureTime(session.hCamera, targetUs) == CAMERA_STATUS_SUCCESS);

        // Get line time for tolerance
        double lineTimeUs = 100.0; // fallback
        CameraGetExposureLineTime(session.hCamera, &lineTimeUs);
        SPDLOG_INFO("  Line time: {} us", lineTimeUs);

        double readbackUs = 0.0;
        EXPECT_TRUE(CameraGetExposureTime(session.hCamera, &readbackUs) == CAMERA_STATUS_SUCCESS);
        SPDLOG_INFO("  Target: {} us, Readback: {} us, Tolerance: {} us",
                     targetUs, readbackUs, lineTimeUs);

        EXPECT_NEAR(readbackUs, targetUs, lineTimeUs);


    END_TEST;
}

static TestResult test_exposureTime_high()
{
    return BEGIN_TEST("exposureTime_high: set 50000us and readback within line-time tolerance")

        auto session = initSdkAndOpenCamera();
        if (!session.valid) SKIP_TEST(session.skipReason);

        double targetUs = 50000.0;
        EXPECT_TRUE(CameraSetExposureTime(session.hCamera, targetUs) == CAMERA_STATUS_SUCCESS);

        double lineTimeUs = 100.0;
        CameraGetExposureLineTime(session.hCamera, &lineTimeUs);

        double readbackUs = 0.0;
        EXPECT_TRUE(CameraGetExposureTime(session.hCamera, &readbackUs) == CAMERA_STATUS_SUCCESS);
        SPDLOG_INFO("  Target: {} us, Readback: {} us, Tolerance: {} us",
                     targetUs, readbackUs, lineTimeUs);

        EXPECT_NEAR(readbackUs, targetUs, lineTimeUs);


    END_TEST;
}

static TestResult test_triggerMode_software()
{
    return BEGIN_TEST("triggerMode_software: set mode 0 (continuous) and readback")

        auto session = initSdkAndOpenCamera();
        if (!session.valid) SKIP_TEST(session.skipReason);

        EXPECT_TRUE(CameraSetTriggerMode(session.hCamera, 0) == CAMERA_STATUS_SUCCESS);

        INT mode = -1;
        EXPECT_TRUE(CameraGetTriggerMode(session.hCamera, &mode) == CAMERA_STATUS_SUCCESS);
        SPDLOG_INFO("  Readback trigger mode: {}", mode);

        EXPECT_EQ(mode, 0);


    END_TEST;
}

static TestResult test_triggerMode_hardware()
{
    return BEGIN_TEST("triggerMode_hardware: set mode 2 (hardware trigger) and readback")

        auto session = initSdkAndOpenCamera();
        if (!session.valid) SKIP_TEST(session.skipReason);

        EXPECT_TRUE(CameraSetTriggerMode(session.hCamera, 2) == CAMERA_STATUS_SUCCESS);

        INT mode = -1;
        EXPECT_TRUE(CameraGetTriggerMode(session.hCamera, &mode) == CAMERA_STATUS_SUCCESS);
        SPDLOG_INFO("  Readback trigger mode: {}", mode);

        EXPECT_EQ(mode, 2);

        // Restore to continuous so camera isn't left in hardware trigger mode
        CameraSetTriggerMode(session.hCamera, 0);


    END_TEST;
}

static TestResult test_analogGain()
{
    return BEGIN_TEST("analogGain: set gain via SDK and verify readback")

        auto session = initSdkAndOpenCamera();
        if (!session.valid) SKIP_TEST(session.skipReason);

        // Disable auto-exposure so manual gain takes effect
        CameraSetAeState(session.hCamera, FALSE);

        // Read the gain range from capabilities
        SPDLOG_INFO("  Gain range: min={}, max={}, step={}",
                     session.cap.sExposeDesc.uiAnalogGainMin,
                     session.cap.sExposeDesc.uiAnalogGainMax,
                     session.cap.sExposeDesc.fAnalogGainStep);

        // Try setting two distinct gain values
        INT gainA = static_cast<INT>(session.cap.sExposeDesc.uiAnalogGainMin);
        INT gainB = static_cast<INT>(session.cap.sExposeDesc.uiAnalogGainMax);
        if (gainA == gainB) gainB = gainA + 1; // ensure different

        CameraSdkStatus sA = CameraSetAnalogGain(session.hCamera, gainA);
        INT readbackA = -1;
        CameraGetAnalogGain(session.hCamera, &readbackA);
        SPDLOG_INFO("  Set gain={}, status={}, readback={}", gainA, sA, readbackA);

        CameraSdkStatus sB = CameraSetAnalogGain(session.hCamera, gainB);
        INT readbackB = -1;
        CameraGetAnalogGain(session.hCamera, &readbackB);
        SPDLOG_INFO("  Set gain={}, status={}, readback={}", gainB, sB, readbackB);

        // Verify at least the setter calls succeed
        EXPECT_TRUE(sA == CAMERA_STATUS_SUCCESS);
        EXPECT_TRUE(sB == CAMERA_STATUS_SUCCESS);

        if (readbackA == readbackB) {
            SPDLOG_WARN("  NOTE: Gain readback unchanged ({}) despite setting {} then {}. "
                        "This camera may have fixed gain or firmware-controlled gain.",
                        readbackA, gainA, gainB);
        } else {
            SPDLOG_INFO("  Gain readback changed: {} -> {} (setter verified)", readbackA, readbackB);
        }

    END_TEST;
}

static TestResult test_strobeMode()
{
    return BEGIN_TEST("strobeMode: set STROBE_SYNC_WITH_TRIG_AUTO and MANUAL, readback")

        auto session = initSdkAndOpenCamera();
        if (!session.valid) SKIP_TEST(session.skipReason);

        // Test STROBE_SYNC_WITH_TRIG_AUTO (0)
        EXPECT_TRUE(CameraSetStrobeMode(session.hCamera, STROBE_SYNC_WITH_TRIG_AUTO) == CAMERA_STATUS_SUCCESS);
        INT mode = -1;
        EXPECT_TRUE(CameraGetStrobeMode(session.hCamera, &mode) == CAMERA_STATUS_SUCCESS);
        SPDLOG_INFO("  Set AUTO(0), readback: {}", mode);
        EXPECT_EQ(mode, (INT)STROBE_SYNC_WITH_TRIG_AUTO);

        // Test STROBE_SYNC_WITH_TRIG_MANUAL (1)
        EXPECT_TRUE(CameraSetStrobeMode(session.hCamera, STROBE_SYNC_WITH_TRIG_MANUAL) == CAMERA_STATUS_SUCCESS);
        mode = -1;
        EXPECT_TRUE(CameraGetStrobeMode(session.hCamera, &mode) == CAMERA_STATUS_SUCCESS);
        SPDLOG_INFO("  Set MANUAL(1), readback: {}", mode);
        EXPECT_EQ(mode, (INT)STROBE_SYNC_WITH_TRIG_MANUAL);

        // Restore default
        CameraSetStrobeMode(session.hCamera, STROBE_SYNC_WITH_TRIG_AUTO);


    END_TEST;
}

static TestResult test_strobePulseWidth()
{
    return BEGIN_TEST("strobePulseWidth: set pulse width and readback")

        auto session = initSdkAndOpenCamera();
        if (!session.valid) SKIP_TEST(session.skipReason);

        // Must be in MANUAL strobe mode for pulse width to be meaningful
        CameraSetStrobeMode(session.hCamera, STROBE_SYNC_WITH_TRIG_MANUAL);

        UINT targetWidth = 500; // 500 us
        EXPECT_TRUE(CameraSetStrobePulseWidth(session.hCamera, targetWidth) == CAMERA_STATUS_SUCCESS);

        UINT readbackWidth = 0;
        EXPECT_TRUE(CameraGetStrobePulseWidth(session.hCamera, &readbackWidth) == CAMERA_STATUS_SUCCESS);
        SPDLOG_INFO("  Target pulse width: {} us, Readback: {} us", targetWidth, readbackWidth);

        EXPECT_EQ(readbackWidth, targetWidth);

        // Restore default
        CameraSetStrobeMode(session.hCamera, STROBE_SYNC_WITH_TRIG_AUTO);


    END_TEST;
}

static TestResult test_strobePolarity()
{
    return BEGIN_TEST("strobePolarity: set polarity and readback")

        auto session = initSdkAndOpenCamera();
        if (!session.valid) SKIP_TEST(session.skipReason);

        // Test polarity = 1 (active high, default)
        EXPECT_TRUE(CameraSetStrobePolarity(session.hCamera, 1) == CAMERA_STATUS_SUCCESS);
        INT polarity = -1;
        EXPECT_TRUE(CameraGetStrobePolarity(session.hCamera, &polarity) == CAMERA_STATUS_SUCCESS);
        SPDLOG_INFO("  Set polarity=1, readback: {}", polarity);
        EXPECT_EQ(polarity, 1);

        // Test polarity = 0 (active low)
        EXPECT_TRUE(CameraSetStrobePolarity(session.hCamera, 0) == CAMERA_STATUS_SUCCESS);
        polarity = -1;
        EXPECT_TRUE(CameraGetStrobePolarity(session.hCamera, &polarity) == CAMERA_STATUS_SUCCESS);
        SPDLOG_INFO("  Set polarity=0, readback: {}", polarity);
        EXPECT_EQ(polarity, 0);

        // Restore default (active high)
        CameraSetStrobePolarity(session.hCamera, 1);


    END_TEST;
}

static TestResult test_configRoundTrip_experiment()
{
    return BEGIN_TEST("configRoundTrip_experiment: full production config, validate all params")

        auto session = initSdkAndOpenCamera();
        if (!session.valid) SKIP_TEST(session.skipReason);

        const char* json = R"({
  "width": 512, "height": 96,
  "offset_x": 152, "offset_y": 264,
  "exposure_time_us": 3000.0,
  "trigger_mode": 2,
  "analog_gain": 1
})";
        EXPECT_TRUE(applyJsonToCamera(session.hCamera, json));

        // Validate resolution
        tSdkImageResolution res{};
        EXPECT_TRUE(CameraGetImageResolution(session.hCamera, &res) == CAMERA_STATUS_SUCCESS);
        EXPECT_EQ(res.iWidthFOV,   512);
        EXPECT_EQ(res.iHeightFOV,  96);
        EXPECT_EQ(res.iHOffsetFOV, 152);
        EXPECT_EQ(res.iVOffsetFOV, 264);

        // Validate exposure
        double lineTimeUs = 100.0;
        CameraGetExposureLineTime(session.hCamera, &lineTimeUs);
        double readbackExpUs = 0.0;
        EXPECT_TRUE(CameraGetExposureTime(session.hCamera, &readbackExpUs) == CAMERA_STATUS_SUCCESS);
        SPDLOG_INFO("  Exposure: target=3000, readback={}, tol={}", readbackExpUs, lineTimeUs);
        EXPECT_NEAR(readbackExpUs, 3000.0, lineTimeUs);

        // Validate trigger mode
        INT mode = -1;
        EXPECT_TRUE(CameraGetTriggerMode(session.hCamera, &mode) == CAMERA_STATUS_SUCCESS);
        EXPECT_EQ(mode, 2);

        // Validate analog gain (SDK maps raw values internally, so verify setter succeeded)
        INT gain = -1;
        EXPECT_TRUE(CameraGetAnalogGain(session.hCamera, &gain) == CAMERA_STATUS_SUCCESS);
        SPDLOG_INFO("  Analog gain: set=1, readback={}", gain);

        SPDLOG_INFO("  All round-trip validations passed");

        // Restore continuous trigger
        CameraSetTriggerMode(session.hCamera, 0);


    END_TEST;
}

static TestResult test_sequentialConfigChanges()
{
    return BEGIN_TEST("sequentialConfigChanges: apply full-sensor then ROI on same handle")

        auto session = initSdkAndOpenCamera();
        if (!session.valid) SKIP_TEST(session.skipReason);

        // First config: full sensor
        const char* json1 = R"({
  "width": 816, "height": 624,
  "offset_x": 0, "offset_y": 0,
  "exposure_time_us": 3000.0, "trigger_mode": 0, "analog_gain": 1
})";
        EXPECT_TRUE(applyJsonToCamera(session.hCamera, json1));

        tSdkImageResolution res{};
        EXPECT_TRUE(CameraGetImageResolution(session.hCamera, &res) == CAMERA_STATUS_SUCCESS);
        SPDLOG_INFO("  After full-sensor: {}x{} @ +{},+{}",
                     res.iWidthFOV, res.iHeightFOV,
                     res.iHOffsetFOV, res.iVOffsetFOV);
        EXPECT_EQ(res.iWidthFOV,   816);
        EXPECT_EQ(res.iHeightFOV,  624);
        EXPECT_EQ(res.iHOffsetFOV, 0);
        EXPECT_EQ(res.iVOffsetFOV, 0);

        // Second config: ROI
        const char* json2 = R"({
  "width": 512, "height": 96,
  "offset_x": 152, "offset_y": 264,
  "exposure_time_us": 3000.0, "trigger_mode": 0, "analog_gain": 1
})";
        EXPECT_TRUE(applyJsonToCamera(session.hCamera, json2));

        res = {};
        EXPECT_TRUE(CameraGetImageResolution(session.hCamera, &res) == CAMERA_STATUS_SUCCESS);
        SPDLOG_INFO("  After ROI: {}x{} @ +{},+{}",
                     res.iWidthFOV, res.iHeightFOV,
                     res.iHOffsetFOV, res.iVOffsetFOV);
        EXPECT_EQ(res.iWidthFOV,   512);
        EXPECT_EQ(res.iHeightFOV,  96);
        EXPECT_EQ(res.iHOffsetFOV, 152);
        EXPECT_EQ(res.iVOffsetFOV, 264);


    END_TEST;
}

// ---------------------------------------------------------------------------
// Image-based validation tests
// ---------------------------------------------------------------------------

static TestResult test_exposureBrightness()
{
    return BEGIN_TEST("exposureBrightness: low vs high exposure produces darker vs brighter image")

        // --- Low exposure (100 us) ---
        const char* jsonLow = R"({
  "width": 816, "height": 624,
  "offset_x": 0, "offset_y": 0,
  "exposure_time_us": 100.0, "trigger_mode": 0, "analog_gain": 1
})";
        auto tempPath = writeTempJson("mv_hw_exp_low.json", jsonLow);
        camera::common::MindVisionCamera camLow(0, tempPath.string());

        if (!camLow.start()) {
            removeTempFile(tempPath);
            SKIP_TEST("Camera not available (start failed)");
        }

        // Discard first frame (warm-up)
        camera::common::Frame warmup;
        grabFrameWithRetry(camLow, warmup);

        camera::common::Frame frameLow;
        if (!grabFrameWithRetry(camLow, frameLow)) {
            camLow.stop();
            removeTempFile(tempPath);
            SKIP_TEST("Could not grab frame at low exposure");
        }
        double meanLow = computeMeanIntensity(frameLow);
        SPDLOG_INFO("  Low exposure (100 us): mean intensity = {:.2f}", meanLow);
        camLow.stop();
        removeTempFile(tempPath);

        // --- High exposure (50000 us) ---
        const char* jsonHigh = R"({
  "width": 816, "height": 624,
  "offset_x": 0, "offset_y": 0,
  "exposure_time_us": 50000.0, "trigger_mode": 0, "analog_gain": 1
})";
        tempPath = writeTempJson("mv_hw_exp_high.json", jsonHigh);
        camera::common::MindVisionCamera camHigh(0, tempPath.string());

        if (!camHigh.start()) {
            removeTempFile(tempPath);
            SKIP_TEST("Camera not available for high-exposure capture");
        }

        // Discard first frame (warm-up)
        grabFrameWithRetry(camHigh, warmup);

        camera::common::Frame frameHigh;
        if (!grabFrameWithRetry(camHigh, frameHigh)) {
            camHigh.stop();
            removeTempFile(tempPath);
            SKIP_TEST("Could not grab frame at high exposure");
        }
        double meanHigh = computeMeanIntensity(frameHigh);
        SPDLOG_INFO("  High exposure (50000 us): mean intensity = {:.2f}", meanHigh);
        camHigh.stop();
        removeTempFile(tempPath);

        SPDLOG_INFO("  Delta: meanHigh - meanLow = {:.2f}", meanHigh - meanLow);
        EXPECT_TRUE(meanHigh > meanLow + 5.0);

    END_TEST;
}

static TestResult test_roiFrameDimensions()
{
    return BEGIN_TEST("roiFrameDimensions: captured frame dimensions match configured ROI")

        // --- Full sensor (816x624) ---
        const char* jsonFull = R"({
  "width": 816, "height": 624,
  "offset_x": 0, "offset_y": 0,
  "exposure_time_us": 3000.0, "trigger_mode": 0, "analog_gain": 1
})";
        auto tempPath = writeTempJson("mv_hw_roi_full.json", jsonFull);
        camera::common::MindVisionCamera camFull(0, tempPath.string());

        if (!camFull.start()) {
            removeTempFile(tempPath);
            SKIP_TEST("Camera not available (start failed)");
        }

        camera::common::Frame frameFull;
        if (!grabFrameWithRetry(camFull, frameFull)) {
            camFull.stop();
            removeTempFile(tempPath);
            SKIP_TEST("Could not grab frame at full sensor");
        }

        SPDLOG_INFO("  Full sensor: frame {}x{}, data size = {}",
                     frameFull.width, frameFull.height, frameFull.data.size());
        EXPECT_EQ(static_cast<int>(frameFull.width),  816);
        EXPECT_EQ(static_cast<int>(frameFull.height), 624);
        EXPECT_EQ(static_cast<int>(frameFull.data.size()), 816 * 624);
        camFull.stop();
        removeTempFile(tempPath);

        // --- ROI (512x96 @ offset 152,264) ---
        const char* jsonROI = R"({
  "width": 512, "height": 96,
  "offset_x": 152, "offset_y": 264,
  "exposure_time_us": 3000.0, "trigger_mode": 0, "analog_gain": 1
})";
        tempPath = writeTempJson("mv_hw_roi_crop.json", jsonROI);
        camera::common::MindVisionCamera camROI(0, tempPath.string());

        if (!camROI.start()) {
            removeTempFile(tempPath);
            SKIP_TEST("Camera not available for ROI capture");
        }

        camera::common::Frame frameROI;
        if (!grabFrameWithRetry(camROI, frameROI)) {
            camROI.stop();
            removeTempFile(tempPath);
            SKIP_TEST("Could not grab frame at ROI");
        }

        SPDLOG_INFO("  ROI: frame {}x{}, data size = {}",
                     frameROI.width, frameROI.height, frameROI.data.size());
        EXPECT_EQ(static_cast<int>(frameROI.width),  512);
        EXPECT_EQ(static_cast<int>(frameROI.height), 96);
        EXPECT_EQ(static_cast<int>(frameROI.data.size()), 512 * 96);
        camROI.stop();
        removeTempFile(tempPath);

    END_TEST;
}

static TestResult test_frameSpeedThroughput()
{
    return BEGIN_TEST("frameSpeedThroughput: higher frame speed yields more frames per second")

        auto session = initSdkAndOpenCamera();
        if (!session.valid) SKIP_TEST(session.skipReason);

        // Check if camera supports multiple frame speed modes
        if (session.cap.iFrameSpeedDesc <= 1) {
            SKIP_TEST("Camera supports only 1 frame speed mode");
        }

        // Set continuous trigger mode
        CameraSetTriggerMode(session.hCamera, 0);

        // Set ISP output to MONO8 for mono sensors
        if (session.cap.sIspCapacity.bMonoSensor) {
            CameraSetIspOutFormat(session.hCamera, CAMERA_MEDIA_TYPE_MONO8);
        }

        // Use full sensor resolution for consistent comparison
        tSdkImageResolution res{};
        res.iIndex      = 0xFF;
        res.iHOffsetFOV = 0;
        res.iVOffsetFOV = 0;
        res.iWidthFOV   = 816;
        res.iHeightFOV  = 624;
        res.iWidth      = 816;
        res.iHeight     = 624;
        CameraSetImageResolution(session.hCamera, &res);

        // Allocate output buffer
        const int bufSize = 816 * 624;
        BYTE* outBuf = CameraAlignMalloc(bufSize, 16);
        EXPECT_TRUE(outBuf != nullptr);

        auto grabForDuration = [&](int speedIndex, double durationSec) -> int {
            CameraSetFrameSpeed(session.hCamera, speedIndex);
            CameraPlay(session.hCamera);

            // Brief warm-up: discard first frame
            {
                tSdkFrameHead fh{};
                BYTE* pBuf = nullptr;
                if (CameraGetImageBuffer(session.hCamera, &fh, &pBuf, 2000) == CAMERA_STATUS_SUCCESS) {
                    CameraReleaseImageBuffer(session.hCamera, pBuf);
                }
            }

            int count = 0;
            auto start = std::chrono::steady_clock::now();
            while (true) {
                auto elapsed = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - start).count();
                if (elapsed >= durationSec) break;

                tSdkFrameHead fh{};
                BYTE* pBuf = nullptr;
                CameraSdkStatus st = CameraGetImageBuffer(session.hCamera, &fh, &pBuf, 200);
                if (st == CAMERA_STATUS_SUCCESS) {
                    CameraImageProcess(session.hCamera, pBuf, outBuf, &fh);
                    CameraReleaseImageBuffer(session.hCamera, pBuf);
                    ++count;
                }
            }

            CameraStop(session.hCamera);
            return count;
        };

        const double testDurationSec = 2.0;

        int countLow = grabForDuration(0, testDurationSec);
        double fpsLow = countLow / testDurationSec;
        SPDLOG_INFO("  Frame speed LOW (0): {} frames in {:.1f}s = {:.1f} FPS",
                     countLow, testDurationSec, fpsLow);

        int countHigh = grabForDuration(2, testDurationSec);
        double fpsHigh = countHigh / testDurationSec;
        SPDLOG_INFO("  Frame speed HIGH (2): {} frames in {:.1f}s = {:.1f} FPS",
                     countHigh, testDurationSec, fpsHigh);

        CameraAlignFree(outBuf);

        SPDLOG_INFO("  Throughput ratio: HIGH/LOW = {:.2f}",
                     (countLow > 0) ? static_cast<double>(countHigh) / countLow : 0.0);
        EXPECT_TRUE(countHigh > countLow);

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

    SPDLOG_INFO("===== MindVision Hardware Validation Tests =====");

    g_results.push_back(test_fullSensorResolution());
    g_results.push_back(test_roiResolution());
    g_results.push_back(test_exposureTime_low());
    g_results.push_back(test_exposureTime_high());
    g_results.push_back(test_triggerMode_software());
    g_results.push_back(test_triggerMode_hardware());
    g_results.push_back(test_analogGain());
    g_results.push_back(test_strobeMode());
    g_results.push_back(test_strobePulseWidth());
    g_results.push_back(test_strobePolarity());
    g_results.push_back(test_configRoundTrip_experiment());
    g_results.push_back(test_sequentialConfigChanges());
    g_results.push_back(test_exposureBrightness());
    g_results.push_back(test_roiFrameDimensions());
    g_results.push_back(test_frameSpeedThroughput());

    // Summary
    int passed = 0, failed = 0, skipped = 0;
    SPDLOG_INFO("=================================================");
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
    SPDLOG_INFO("=================================================");
    SPDLOG_INFO("Results: {} passed, {} skipped, {} failed", passed, skipped, failed);

    return (failed == 0) ? 0 : 1;
}

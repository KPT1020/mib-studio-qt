// mindvision_config_apply_test
//
// Guards the shared config-apply pipeline (backend::camera::mindvision::
// applyJsonFileToCamera) that both MindVisionCamera::start() and
// CameraControlService::applyMindVisionConfig now route through, replacing two
// drifted copies (the service used to apply only a 7-field subset). The
// file-open and parse layers are SDK-free and always compiled, so this test
// runs on the Linux stub build: it proves errors and clamp warnings propagate
// through the apply layer and that control reaches the SDK boundary. The SDK
// setter sequence itself only runs on hardware (see hardware.mindvision_apply).

#include "backend/camera/mindvision/MindVisionConfigApply.h"

#include "support/assert.h"
#include "support/tempdir.h"

#include <cstdio>
#include <fstream>
#include <string>

namespace mv = backend::camera::mindvision;

namespace {
std::string writeFile(const mib::test::TempDir& td, const char* name, const char* text)
{
    const auto path = (td / name).string();
    std::ofstream out(path);
    out << text;
    return path;
}

bool contains(const std::string& haystack, const char* needle)
{
    return haystack.find(needle) != std::string::npos;
}
} // namespace

int main()
{
    mib::test::TempDir td("mib_mv_apply");
    // No camera handle exists in this test; the stub apply never touches it and
    // the file/parse failures return before any SDK use.
    constexpr int kNoHandle = -1;

    // 1) Nonexistent file: fatal, names the path.
    {
        const auto r = mv::applyJsonFileToCamera(kNoHandle, (td / "missing.json").string());
        MIB_REQUIRE(!r.ok, "missing file is fatal");
        MIB_EXPECT(contains(r.error, "missing.json"), "error names the file");
    }

    // 2) Malformed JSON: fatal parse error, names the path.
    {
        const auto path = writeFile(td, "bad.json", "{ not valid json ");
        const auto r = mv::applyJsonFileToCamera(kNoHandle, path);
        MIB_REQUIRE(!r.ok, "malformed JSON is fatal");
        MIB_EXPECT(contains(r.error, "parse"), "error mentions the parse failure");
        MIB_EXPECT(contains(r.error, "bad.json"), "error names the file");
    }

    // 3) Valid config: parse succeeds and control reaches the SDK boundary.
    //    On the stub build that boundary reports "disabled at build time"; the
    //    parsed config is still returned either way.
    {
        const auto path = writeFile(td, "good.json", R"({
            "width": 640, "height": 128, "exposure_time_us": 2500.0,
            "trigger_mode": 1, "analog_gain": 4, "trigger_output_index": 0
        })");
        const auto r = mv::applyJsonFileToCamera(kNoHandle, path);
#if MIB_HAS_MINDVISION
        // Real-SDK build without a device: outcome depends on the SDK's handle
        // validation, so only the parse layer is asserted here.
        MIB_EXPECT(r.config.width == 640, "parsed config returned");
#else
        MIB_REQUIRE(!r.ok, "stub build cannot apply");
        MIB_EXPECT(contains(r.error, "disabled at build time"), "stub reports SDK unavailable");
#endif
        MIB_EXPECT(r.config.width == 640 && r.config.height == 128, "parsed ROI in result");
        MIB_EXPECT(r.config.triggerOutputIndex == 0, "trigger_output_index in result");
    }

    // 4) Clamp warnings surface through the apply layer, not just parseConfig.
    {
        const auto path = writeFile(td, "clamped.json",
                                    R"({"strobe_pulse_width_us": -5, "analog_gain": 0})");
        const auto r = mv::applyJsonFileToCamera(kNoHandle, path);
        MIB_EXPECT(r.warnings.size() >= 2, "each clamped field warns through ApplyResult");
        bool sawStrobe = false;
        for (const auto& w : r.warnings) {
            if (contains(w, "strobe_pulse_width_us")) sawStrobe = true;
        }
        MIB_EXPECT(sawStrobe, "clamp warning names the field");
        MIB_EXPECT(r.config.strobePulseUs == 0 && r.config.analogGain == 1,
                   "clamped values in returned config");
    }

    if (mib::test::exitCode() == 0) {
        std::printf("MindVision shared config-apply pipeline verified\n");
    }
    return mib::test::exitCode();
}

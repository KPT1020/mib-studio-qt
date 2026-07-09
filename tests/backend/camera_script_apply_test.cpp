// camera_script_apply_test
//
// Hardware-independent coverage of the EGrabber camera-script entry point
// (AppBackend::applyCameraScriptFromFile) — the path used to drive the LED /
// strobe on the EGrabber camera. Verifies the precondition guards return cleanly
// with actionable errors and never touch the device for invalid input. The
// actual on-device script run is covered by hardware.egrabber_script.

#include "backend/app/AppBackend.h"

#include "support/assert.h"
#include "support/tempdir.h"

#include <QtGlobal>
#include <QByteArray>
#include <QCoreApplication>
#include <QSettings>
#include <QString>

#include <fstream>
#include <string>

int main(int argc, char* argv[])
{
    // Route the startup LUT lookup to a file: URL so initialize() does no
    // network I/O (keeps this unit test fast/offline).
    qputenv("MIB_STUDIO_EMODULUS_LUT_MANIFEST_URL", "file:///nonexistent/lut.json");
    mib::test::TempDir settingsDir("mib_camera_script_settings");
    qputenv("XDG_CONFIG_HOME", QByteArray::fromStdString(settingsDir.path().string()));
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("MIBStudioQtTests"));
    QCoreApplication::setApplicationName(QStringLiteral("camera_script_apply"));
    {
        QSettings settings;
        settings.clear();
        settings.sync();
    }

    mib::test::TempDir td("mib_camera_script");
    backend::AppBackend backend;
    MIB_REQUIRE(backend.initialize((td / "data").string()), "AppBackend initialize");
    const auto externalConfig = td / "external_config.json";
    {
        std::ofstream out(externalConfig, std::ios::binary | std::ios::trunc);
        out << "{\"external\":true}\n";
    }
    {
        QSettings settings;
        settings.setValue(QStringLiteral("Config/ExternalAppConfigPath"), QString::fromStdString(externalConfig.string()));
        settings.sync();
    }

    // No camera selected -> clean refusal, no device access.
    {
        std::string err;
        MIB_EXPECT(!backend.applyCameraScriptFromFile((td / "led.js").string(), &err),
                   "apply script with no camera selected returns false");
        MIB_EXPECT(err == "No hardware camera selected", "clear 'no camera' error");
    }

#if MIB_HAS_EGRABBER
    // With a camera selected (SDK build only), a missing script file must fail at
    // the file precheck WITHOUT opening the device.
    {
        backend.setHardwareCameraSelection(0, 0, "test-cam");
        std::string err;
        const std::string missing = (td / "does_not_exist.js").string();
        MIB_EXPECT(!backend.applyCameraScriptFromFile(missing, &err),
                   "apply missing script returns false");
        MIB_EXPECT(err.find("not found") != std::string::npos,
                   "missing-script error names the problem");
    }
#endif

    {
        QSettings settings;
        settings.remove(QStringLiteral("Config/ExternalAppConfigPath"));
        settings.sync();
    }

    if (mib::test::exitCode() == 0) {
        std::printf("camera-script (LED) apply guards verified\n");
    }
    return mib::test::exitCode();
}

// hw_egrabber_script_test  (LABEL: hardware) — runs only against a real EGrabber
// camera. Applies a camera script (the EGrabber LED/strobe control mechanism)
// and asserts it is accepted by the device.
//
// Set MIB_TEST_EGRABBER_SCRIPT to a script file path (e.g. an LED-on script).
// Optional: MIB_TEST_EGRABBER_IF / MIB_TEST_EGRABBER_DEV (default 0/0).
// Skips when MIB_TEST_EGRABBER_SCRIPT is absent or on a non-EGrabber build.

#include "backend/app/AppBackend.h"

#include "support/assert.h"
#include "support/hardware.h"
#include "support/tempdir.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QSettings>
#include <QString>

#include <cstdio>
#include <fstream>
#include <string>

int main(int argc, char* argv[])
{
    mib::test::TempDir settingsDir("mib_hw_egrabber_script_settings");
    qputenv("XDG_CONFIG_HOME", QByteArray::fromStdString(settingsDir.path().string()));
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("MIBStudioQtTests"));
    QCoreApplication::setApplicationName(QStringLiteral("hw_egrabber_script"));
    {
        QSettings settings;
        settings.clear();
        settings.sync();
    }
    const char* scriptPath = mib::test::requireDeviceEnv("MIB_TEST_EGRABBER_SCRIPT");

#if !MIB_HAS_EGRABBER
    std::printf("SKIP: built without the EGrabber SDK\n");
    return mib::test::kSkipExitCode;
#else
    const int ifIdx = mib::test::envInt("MIB_TEST_EGRABBER_IF", 0);
    const int devIdx = mib::test::envInt("MIB_TEST_EGRABBER_DEV", 0);

    mib::test::TempDir td("mib_hw_egrabber_script");
    const auto externalConfig = td / "hardware_config.json";
    {
        std::ofstream out(externalConfig, std::ios::binary | std::ios::trunc);
        out << "{\"external\":true}\n";
    }
    {
        QSettings settings;
        settings.setValue(QStringLiteral("Config/ExternalAppConfigPath"), QString::fromStdString(externalConfig.string()));
        settings.sync();
    }
    backend::AppBackend backend;
    MIB_REQUIRE(backend.initialize((td / "data").string()), "AppBackend initialize");

    backend.setHardwareCameraSelection(ifIdx, devIdx, "egrabber");
    MIB_REQUIRE(backend.isMindVisionCameraSelected() == false, "hardware (EGrabber) selected");

    std::string err;
    const bool ok = backend.applyCameraScriptFromFile(scriptPath, &err);
    MIB_EXPECT(ok, std::string("apply EGrabber LED/camera script: ") + err);

    if (mib::test::exitCode() == 0) std::printf("EGrabber camera-script (LED) applied OK\n");
    {
        QSettings settings;
        settings.remove(QStringLiteral("Config/ExternalAppConfigPath"));
        settings.sync();
    }
    return mib::test::exitCode();
#endif
}

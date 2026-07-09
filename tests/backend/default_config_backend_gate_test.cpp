#include "backend/app/AppBackend.h"
#include "backend/camera/mock/MockCamera.h"
#include "backend/recording/Hdf5Service.h"
#include "backend/services/CaptureService.h"
#include "frontend/system/DefaultConfigTrustGate.h"

#include "support/assert.h"
#include "support/tempdir.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QSettings>
#include <QtGlobal>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace
{
void writeText(const std::filesystem::path& path, const char* text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

void setExternalConfig(const std::filesystem::path& path)
{
    QSettings settings;
    settings.setValue(QStringLiteral("Config/ExternalAppConfigPath"), QString::fromStdString(path.string()));
    settings.sync();
}

void clearConfigState()
{
    QSettings settings;
    settings.clear();
    settings.sync();
}
} // namespace

int main(int argc, char* argv[])
{
    mib::test::TempDir xdg("mib_default_backend_gate_xdg");
    qputenv("XDG_CONFIG_HOME", QByteArray::fromStdString(xdg.path().string()));
    qputenv("MIB_DEFAULT_CONFIG_PATH_FOR_TESTS", QByteArray::fromStdString((xdg.path() / "default_config.json").string()));
    qputenv("MIB_STUDIO_EMODULUS_LUT_MANIFEST_URL", "file:///nonexistent/lut.json");

    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("MIBStudioQtTests"));
    QCoreApplication::setApplicationName(QStringLiteral("default_config_backend_gate"));
    clearConfigState();

    const auto defaultPath = std::filesystem::path(frontend::DefaultConfigTrustGate::defaultConfigPath().toStdString());
    writeText(defaultPath, "{\"default\":true}\n");

    mib::test::TempDir td("mib_default_backend_gate");
    const auto frameDir = td / "frames";
    std::filesystem::create_directories(frameDir);
    const cv::Mat frame(16, 16, CV_8UC1, cv::Scalar(180));
    MIB_REQUIRE(cv::imwrite((frameDir / "frame_000.png").string(), frame), "write mock frame");

    backend::AppBackend backend;
    MIB_REQUIRE(backend.initialize((td / "data").string()), "AppBackend initialize");

    camera::mock::MockCameraOptions options;
    options.folder = frameDir;
    options.frameInterval = std::chrono::milliseconds(1);
    options.loopFiles = true;
    backend.configureMockCamera(options);
    MIB_REQUIRE(backend.capture().start(), "mock capture starts");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    const auto blockedRecording = td / "blocked_recording.h5";
    MIB_EXPECT(!backend.startFrameRecording(blockedRecording.string()),
               "unconfirmed default blocks frame recording");
    MIB_EXPECT(!backend.isFrameRecording(), "recording flag is not set when blocked");
    MIB_EXPECT(!backend.hdf5().isFileOpen(), "HDF5 file is not opened when recording is blocked");

    backend.setMindVisionCameraSelection(0, "test-mindvision");
    std::string err;
    MIB_EXPECT(!backend.applyMindVisionConfigFromFile((td / "missing_mindvision.json").string(), &err),
               "unconfirmed default blocks MindVision apply before SDK/file path handling");
    MIB_EXPECT(err.find("Using default config") != std::string::npos,
               "apply guard reports default-config state");

    const auto externalPath = td / "external_config.json";
    writeText(externalPath, "{\"external\":true}\n");
    setExternalConfig(externalPath);

    const auto allowedRecording = td / "allowed_recording.h5";
    MIB_EXPECT(backend.startFrameRecording(allowedRecording.string()),
               "external config permits frame recording");
    backend.stopFrameRecording();
    MIB_EXPECT(!backend.isFrameRecording(), "recording stops cleanly after allowed start");

    backend.capture().stop();
    backend.shutdown();
    clearConfigState();
    return mib::test::exitCode();
}

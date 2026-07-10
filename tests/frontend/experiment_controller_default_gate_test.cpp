#include "frontend/controllers/ExperimentController.h"

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
#include <QString>
#include <QtGlobal>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace
{
void writeText(const std::filesystem::path& path, const char* text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

void clearConfigState()
{
    QSettings settings;
    settings.clear();
    settings.sync();
}

void setExternalConfig(const std::filesystem::path& path)
{
    QSettings settings;
    settings.setValue(QStringLiteral("Config/ExternalAppConfigPath"),
                      QString::fromStdString(path.string()));
    settings.sync();
}
} // namespace

int main(int argc, char* argv[])
{
    mib::test::TempDir xdg("mib_experiment_controller_gate_xdg");
    qputenv("XDG_CONFIG_HOME", QByteArray::fromStdString(xdg.path().string()));
    qputenv("MIB_DEFAULT_CONFIG_PATH_FOR_TESTS",
            QByteArray::fromStdString((xdg.path() / "default_config.json").string()));
    qputenv("MIB_STUDIO_EMODULUS_LUT_MANIFEST_URL", "file:///nonexistent/lut.json");

    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("MIBStudioQtTests"));
    QCoreApplication::setApplicationName(QStringLiteral("experiment_controller_default_gate"));
    clearConfigState();

    const auto defaultPath = std::filesystem::path(
        frontend::DefaultConfigTrustGate::defaultConfigPath().toStdString());
    writeText(defaultPath, "{\"default\":true}\n");

    mib::test::TempDir td("mib_experiment_controller_gate");
    const auto frameDir = td / "frames";
    std::filesystem::create_directories(frameDir);
    const cv::Mat frame(16, 16, CV_8UC1, cv::Scalar(120));
    MIB_REQUIRE(cv::imwrite((frameDir / "frame_000.png").string(), frame),
                "write mock frame");

    backend::AppBackend backend;
    MIB_REQUIRE(backend.initialize((td / "data").string()), "AppBackend initialize");

    camera::mock::MockCameraOptions options;
    options.folder = frameDir;
    options.frameInterval = std::chrono::milliseconds(1);
    options.loopFiles = true;
    backend.configureMockCamera(options);
    MIB_REQUIRE(backend.capture().start(), "mock capture starts");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    frontend::ExperimentController controller(backend);
    QString error;
    const auto blockedExperiment = td / "blocked_experiment.h5";
    MIB_EXPECT(!controller.startExperiment(QString::fromStdString(blockedExperiment.string()), &error),
               "unconfirmed default blocks experiment controller start");
    MIB_EXPECT(error.contains(QStringLiteral("Using default config")),
               "controller block message names default-config state");
    MIB_EXPECT(controller.state() == frontend::ExperimentController::State::Idle,
               "controller remains idle when blocked");
    MIB_EXPECT(!backend.hdf5().isFileOpen(),
               "HDF5 file is not opened when experiment is blocked");

    const auto externalPath = td / "external_config.json";
    writeText(externalPath, "{\"external\":true}\n");
    setExternalConfig(externalPath);

    const auto allowedExperiment = td / "allowed_experiment.h5";
    error.clear();
    MIB_EXPECT(controller.startExperiment(QString::fromStdString(allowedExperiment.string()), &error),
               "external config permits experiment controller start");
    MIB_EXPECT(controller.state() == frontend::ExperimentController::State::Active,
               "controller becomes active after allowed start");
    MIB_EXPECT(backend.hdf5().isFileOpen(), "HDF5 opens after allowed start");
    MIB_EXPECT(controller.stopExperiment(&error), "allowed experiment stops cleanly");
    MIB_EXPECT(controller.state() == frontend::ExperimentController::State::Idle,
               "controller returns idle after stop");
    MIB_EXPECT(!backend.hdf5().isFileOpen(), "HDF5 closes after stop");

    backend.capture().stop();
    backend.shutdown();
    clearConfigState();
    return mib::test::exitCode();
}

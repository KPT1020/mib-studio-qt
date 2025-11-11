#include <QApplication>
#include <QMessageBox>

#include "backend/AppBackend.h"
#include "frontend/MainWindow.h"
#include "frontend/MockConfigDialog.h"

#include "camera/mock/MockCamera.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <algorithm>

namespace {

std::chrono::microseconds intervalFromFps(double fps) {
    if (fps <= 0.0) {
        return std::chrono::milliseconds(33);
    }
    const double micros = 1'000'000.0 / fps;
    const auto rounded = static_cast<long long>(std::llround(micros));
    return std::chrono::microseconds(std::max<long long>(1, rounded));
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    frontend::MockConfigDialog dialog;
    if (dialog.exec() != QDialog::Accepted) {
        return 0;
    }

    const QString folder = dialog.folderPath();
    if (folder.isEmpty()) {
        QMessageBox::warning(nullptr,
                             QObject::tr("Mock Camera"),
                             QObject::tr("Please select a folder containing image frames."));
        return 0;
    }

    backend::AppBackend backend;
    backend.initialize("data");

    camera::mock::MockCameraOptions options;
#ifdef _WIN32
    options.folder = std::filesystem::path(folder.toStdWString());
#else
    options.folder = std::filesystem::path(folder.toStdString());
#endif
    if (!std::filesystem::exists(options.folder) || !std::filesystem::is_directory(options.folder)) {
        QMessageBox::warning(nullptr,
                             QObject::tr("Mock Camera"),
                             QObject::tr("The selected folder does not exist or is not a directory."));
        return 0;
    }
    options.frameInterval = intervalFromFps(dialog.framesPerSecond());
    options.loopFiles = true;

    backend.configureMockCamera(options);

    MainWindow w(backend);
    w.resize(960, 600);
    w.show();

    return app.exec();
}



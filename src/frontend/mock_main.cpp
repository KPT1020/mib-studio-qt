#include <QApplication>
#include <QMessageBox>
#include <QCoreApplication>
#include <QDir>

#include "backend/AppBackend.h"
#include "frontend/MainWindow.h"
#include "frontend/MockConfigDialog.h"

#include "camera/mock/MockCamera.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <algorithm>
#include <exception>
#include <iostream>
#ifdef _WIN32
#define NOMINMAX  // Prevent Windows.h from defining min/max macros
#include <windows.h>
#include <shlobj.h>
#endif

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
    try {
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

        // Get executable directory and resolve data path
        QString exeDir = QCoreApplication::applicationDirPath();
        QString dataDir = QDir(exeDir).filePath("data");
        std::string dataDirStd = dataDir.toStdString();

        std::cout << "MIB Studio Qt (Mock Mode) starting..." << std::endl;
        std::cout << "Executable directory: " << exeDir.toStdString() << std::endl;
        std::cout << "Data directory: " << dataDirStd << std::endl;

        backend::AppBackend backend;
        if (!backend.initialize(dataDirStd)) {
            // Determine log location (may be in user AppData if installed in Program Files)
            QString logLocation = dataDir + "\\logs\\app.log";
#ifdef _WIN32
            QString dataDirLower = dataDir.toLower();
            if (dataDirLower.contains("program files")) {
                char appDataPath[MAX_PATH];
                if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appDataPath))) {
                    logLocation = QString::fromStdString(std::string(appDataPath) + "\\MIB_Studio_Qt\\logs\\app.log");
                }
            }
#endif
            QString errorMsg = QString("Failed to initialize application backend.\n\n"
                                      "Data directory: %1\n"
                                      "Log file: %2\n\n"
                                      "Please check:\n"
                                      "- File permissions\n"
                                      "- Available disk space\n"
                                      "- Antivirus software blocking file access")
                                      .arg(dataDir, logLocation);
            QMessageBox::critical(nullptr, "MIB Studio Qt - Initialization Error", errorMsg);
            return 1;
        }

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

        std::cout << "Application started successfully (Mock Mode)." << std::endl;

        return app.exec();
        
    } catch (const std::exception& e) {
        std::cerr << "ERROR: Unhandled exception: " << e.what() << std::endl;
        QMessageBox::critical(nullptr, "MIB Studio Qt - Fatal Error", 
                             QString("The application encountered a fatal error:\n\n%1\n\n"
                                    "Please check the crash log for details.").arg(e.what()));
        return 1;
    } catch (...) {
        std::cerr << "ERROR: Unknown exception occurred" << std::endl;
        QMessageBox::critical(nullptr, "MIB Studio Qt - Fatal Error", 
                             "The application encountered an unknown fatal error.\n\n"
                             "Please check the crash log for details.");
        return 1;
    }
}



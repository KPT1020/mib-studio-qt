#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "backend/services/Logger.h"
#include "standalone/pump_control/PumpControlMainWindow.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#endif

namespace {
void writeEarlyError(const std::string& errorMsg) {
    try {
#ifdef _WIN32
        QString baseDir;
        char appDataPath[MAX_PATH];
        if (SUCCEEDED(
                SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appDataPath))) {
            baseDir = QString::fromStdString(std::string(appDataPath) + "\\Pump_Control");
        }
#else
        QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#endif
        if (!baseDir.isEmpty()) {
            QDir().mkpath(baseDir);
            const QString crashPath = QDir(baseDir).absoluteFilePath("crash_log.txt");
            std::ofstream logFile(crashPath.toStdString(), std::ios::app);
            if (logFile.is_open()) {
                logFile << errorMsg << std::endl;
            }
        }
        std::cerr << "ERROR: " << errorMsg << std::endl;
    } catch (...) {
    }
}

void showError(const QString& title, const QString& message) {
#ifdef _WIN32
    QMessageBox::critical(nullptr, title, message);
#else
    std::cerr << title.toStdString() << ": " << message.toStdString() << std::endl;
#endif
}
} // namespace

int main(int argc, char* argv[]) {
    try {
#ifdef _WIN32
#ifdef _DEBUG
        if (AllocConsole()) {
            FILE* pCout = nullptr;
            FILE* pCerr = nullptr;
            freopen_s(&pCout, "CONOUT$", "w", stdout);
            freopen_s(&pCerr, "CONOUT$", "w", stderr);
            std::cout.clear();
            std::cerr.clear();
            std::clog.clear();
        }
#endif
#endif
        QApplication app(argc, argv);
        QCoreApplication::setOrganizationName(QStringLiteral("MIB"));
        QCoreApplication::setApplicationName(QStringLiteral("Pump Control"));

        QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (appDataDir.isEmpty()) {
            appDataDir = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("data");
        }
        QDir().mkpath(appDataDir);

        const QString logDir = QDir(appDataDir).absoluteFilePath("logs");
        QDir().mkpath(logDir);
        backend::services::Logger::init(QDir(logDir).absoluteFilePath("pump_control.log").toStdString());

        PumpControlMainWindow window;
        window.show();
        return app.exec();
    } catch (const std::exception& e) {
        writeEarlyError(std::string("Unhandled exception: ") + e.what());
        if (QCoreApplication::instance() != nullptr) {
            showError(QStringLiteral("Pump Control - Fatal Error"),
                      QStringLiteral("The application encountered a fatal error:\n\n%1").arg(e.what()));
        }
        return 1;
    } catch (...) {
        writeEarlyError("Unknown exception occurred");
        if (QCoreApplication::instance() != nullptr) {
            showError(QStringLiteral("Pump Control - Fatal Error"),
                      QStringLiteral("The application encountered an unknown fatal error."));
        }
        return 1;
    }
}

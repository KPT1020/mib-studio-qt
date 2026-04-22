#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>
#include <QString>

#include <exception>
#include <fstream>
#include <iostream>
#include <string>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include "backend/services/Logger.h"
#include "standalone/pump_control/PumpControlMainWindow.h"

namespace {

void writeEarlyError(const std::string& msg) {
    try {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (!dir.isEmpty()) {
            QDir().mkpath(dir);
            std::ofstream f((dir + "/crash_log.txt").toStdString(), std::ios::app);
            if (f.is_open()) f << msg << std::endl;
        }
        std::cerr << "ERROR: " << msg << std::endl;
    } catch (...) {
        // give up silently
    }
}

void showError(const QString& title, const QString& message) {
    if (QCoreApplication::instance() != nullptr) {
        QMessageBox::critical(nullptr, title, message);
    } else {
        std::cerr << title.toStdString() << ": " << message.toStdString() << std::endl;
    }
}

} // namespace

int main(int argc, char* argv[]) {
    try {
#ifdef _WIN32
#ifdef _DEBUG
        if (AllocConsole()) {
            FILE* pCout; FILE* pCerr;
            freopen_s(&pCout, "CONOUT$", "w", stdout);
            freopen_s(&pCerr, "CONOUT$", "w", stderr);
            std::cout.clear(); std::cerr.clear(); std::clog.clear();
        }
#endif
#endif
        QApplication app(argc, argv);

        QCoreApplication::setOrganizationName(QStringLiteral("MIB"));
        QCoreApplication::setApplicationName(QStringLiteral("Pump Control"));
#ifdef PUMP_CONTROL_VERSION
        QCoreApplication::setApplicationVersion(QStringLiteral(PUMP_CONTROL_VERSION));
#endif

        // Resolve a writable log dir that works on all platforms.
        const QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(logDir);
        const QString logPath = QDir(logDir).absoluteFilePath("logs/pump_control.log");
        backend::services::Logger::init(logPath.toStdString());

        std::cout << "Pump Control starting..." << std::endl;
        std::cout << "Log file: " << logPath.toStdString() << std::endl;

        standalone::pump_control::PumpControlMainWindow w;
        w.show();

        return app.exec();
    } catch (const std::exception& e) {
        const std::string msg = std::string("Unhandled exception: ") + e.what();
        writeEarlyError(msg);
        showError(QObject::tr("Pump Control — Fatal Error"),
                  QString::fromStdString(msg));
        return 1;
    } catch (...) {
        const std::string msg = "Unknown fatal error";
        writeEarlyError(msg);
        showError(QObject::tr("Pump Control — Fatal Error"),
                  QObject::tr("An unknown fatal error occurred."));
        return 1;
    }
}

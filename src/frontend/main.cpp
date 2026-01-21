#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QMessageBox>
#include <QString>

#include "backend/AppBackend.h"
#include "frontend/MainWindow.h"

#include <exception>
#include <iostream>
#include <fstream>
#include <string>
#ifdef _WIN32
#define NOMINMAX  // Prevent Windows.h from defining min/max macros
#include <windows.h>
#include <shlobj.h>
#endif

namespace {
    // Write error to a guaranteed-writable location before logging is available
    void writeEarlyError(const std::string& errorMsg) {
        try {
#ifdef _WIN32
            char tempPath[MAX_PATH];
            if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, tempPath))) {
                std::string logPath = std::string(tempPath) + "\\MIB_Studio_Qt\\crash_log.txt";
                std::ofstream logFile(logPath, std::ios::app);
                if (logFile.is_open()) {
                    logFile << errorMsg << std::endl;
                    logFile.close();
                }
            }
#endif
            // Also write to console if available
            std::cerr << "ERROR: " << errorMsg << std::endl;
        } catch (...) {
            // If even error logging fails, ignore
        }
    }

    // Show error message box (Windows-specific)
    void showError(const QString& title, const QString& message) {
#ifdef _WIN32
        QMessageBox::critical(nullptr, title, message);
#else
        std::cerr << title.toStdString() << ": " << message.toStdString() << std::endl;
#endif
    }
}

int main(int argc, char* argv[]) {
    try {
#ifdef _WIN32
#ifdef _DEBUG
        // Allocate console window for Debug builds
        if (AllocConsole()) {
            // Redirect stdout and stderr to the console
            FILE* pCout;
            FILE* pCerr;
            freopen_s(&pCout, "CONOUT$", "w", stdout);
            freopen_s(&pCerr, "CONOUT$", "w", stderr);
            std::cout.clear();
            std::cerr.clear();
            std::clog.clear();
        }
#endif
#endif
        // Initialize QApplication first
        QApplication app(argc, argv);

        // Application identity/version (used by the updater and About dialogs)
        QCoreApplication::setApplicationName(QStringLiteral("MIB Studio Qt"));
        QCoreApplication::setApplicationVersion(QStringLiteral(MIB_STUDIO_QT_VERSION));
        
        // Get executable directory and resolve data path
        QString exeDir = QCoreApplication::applicationDirPath();
        QString dataDir = QDir(exeDir).filePath("data");
        
        // Convert to std::string for backend
        std::string dataDirStd = dataDir.toStdString();
        
        // Early diagnostic output
        std::cout << "MIB Studio Qt starting..." << std::endl;
        std::cout << "Executable directory: " << exeDir.toStdString() << std::endl;
        std::cout << "Data directory: " << dataDirStd << std::endl;
        
        // Initialize backend with proper path
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
            writeEarlyError("Backend initialization failed: " + dataDirStd);
            showError("MIB Studio Qt - Initialization Error", errorMsg);
            return 1;
        }
        
        // Create and show main window
        MainWindow w(backend);
        w.resize(960, 600);
        w.show();
        
        std::cout << "Application started successfully." << std::endl;
        
        return app.exec();
        
    } catch (const std::exception& e) {
        std::string errorMsg = std::string("Unhandled exception: ") + e.what();
        writeEarlyError(errorMsg);
        // Only show message box if QApplication was successfully created
        try {
            if (QCoreApplication::instance() != nullptr) {
                showError("MIB Studio Qt - Fatal Error", 
                         QString("The application encountered a fatal error:\n\n%1\n\n"
                                "Please check the crash log for details.").arg(e.what()));
            } else {
                std::cerr << "FATAL ERROR: " << errorMsg << std::endl;
            }
        } catch (...) {
            std::cerr << "FATAL ERROR: " << errorMsg << std::endl;
        }
        return 1;
    } catch (...) {
        std::string errorMsg = "Unknown exception occurred";
        writeEarlyError(errorMsg);
        // Only show message box if QApplication was successfully created
        try {
            if (QCoreApplication::instance() != nullptr) {
                showError("MIB Studio Qt - Fatal Error", 
                         "The application encountered an unknown fatal error.\n\n"
                         "Please check the crash log for details.");
            } else {
                std::cerr << "FATAL ERROR: " << errorMsg << std::endl;
            }
        } catch (...) {
            std::cerr << "FATAL ERROR: " << errorMsg << std::endl;
        }
        return 1;
    }
}

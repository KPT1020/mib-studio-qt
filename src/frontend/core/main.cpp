#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QMessageBox>
#include <QRect>
#include <QScreen>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QSysInfo>

#include "backend/app/AppBackend.h"
#include "backend/diagnostics/CrashStateMirror.h"
#include "backend/services/CrashReporter.h"
#include "frontend/core/MainWindow.h"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#ifdef _WIN32
#define NOMINMAX  // Prevent Windows.h from defining min/max macros
#include <windows.h>
#include <shlobj.h>
#endif

namespace {
    QRect computeStartupWindowGeometry(const QSize& preferredSize, const QRect& availableGeometry)
    {
        if (!availableGeometry.isValid())
        {
            return QRect(QPoint(0, 0), preferredSize);
        }

        const int width = std::max(1, std::min(preferredSize.width(), availableGeometry.width()));
        const int height = std::max(1, std::min(preferredSize.height(), availableGeometry.height()));
        QRect boundedGeometry(QPoint(0, 0), QSize(width, height));
        boundedGeometry.moveCenter(availableGeometry.center());

        const int maxLeft = availableGeometry.right() - boundedGeometry.width() + 1;
        const int maxTop = availableGeometry.bottom() - boundedGeometry.height() + 1;
        boundedGeometry.moveLeft(std::clamp(boundedGeometry.left(), availableGeometry.left(), maxLeft));
        boundedGeometry.moveTop(std::clamp(boundedGeometry.top(), availableGeometry.top(), maxTop));
        return boundedGeometry;
    }

    void applyStartupWindowGeometry(QMainWindow& window)
    {
        QScreen* targetScreen = window.screen();
        if (!targetScreen)
        {
            targetScreen = QGuiApplication::primaryScreen();
        }

        const QSize preferredSize(960, 600);
        if (!targetScreen)
        {
            window.resize(preferredSize);
            return;
        }

        const QRect availableGeometry = targetScreen->availableGeometry();
        window.setGeometry(computeStartupWindowGeometry(preferredSize, availableGeometry));
    }

    QString normalizeDisabledServicesCsv(const QString &raw)
    {
        const QStringList parts = raw.split(',', Qt::SkipEmptyParts);
        QStringList normalized;
        normalized.reserve(parts.size());
        for (QString token : parts)
        {
            token = token.trimmed().toLower();
            token.replace('-', '_');
            if (!token.isEmpty() && !normalized.contains(token))
            {
                normalized.push_back(token);
            }
        }
        return normalized.join(',');
    }

    void applyBootDisabledServicesFromSettings()
    {
        // Respect explicit environment configuration first.
        if (!qEnvironmentVariableIsEmpty("MIB_DISABLED_SERVICES"))
        {
            return;
        }

        QSettings settings;
        const QString persisted = normalizeDisabledServicesCsv(
            settings.value("Startup/DisabledServices").toString());
        if (!persisted.isEmpty())
        {
            qputenv("MIB_DISABLED_SERVICES", persisted.toUtf8());
        }
    }

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

    // Resolve a user-writable directory for crash artifacts (.dmp + .json).
    // Falls back to {exeDir}/data/crashes if AppData lookup fails.
    std::filesystem::path resolveCrashDir(const QString& exeDir) {
#ifdef _WIN32
        char appDataPath[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL,
                                       SHGFP_TYPE_CURRENT, appDataPath))) {
            return std::filesystem::path(appDataPath) / "MIB_Studio_Qt" / "crashes";
        }
#endif
        return std::filesystem::path(exeDir.toStdString()) / "data" / "crashes";
    }

    // Install the crash reporter as early as possible. Must be safe to call
    // before QApplication exists (uses Qt only for QString helpers below).
    void installCrashReporter(const QString& exeDir, const std::string& dataDir) {
        backend::services::CrashReporter::Config cfg;
        cfg.crashDir = resolveCrashDir(exeDir);
        cfg.databaseDir = cfg.crashDir / "sentry-db";
        cfg.release = std::string("mib_studio_qt@") + MIB_STUDIO_QT_VERSION;
        cfg.environment =
#ifdef NDEBUG
            "production";
#else
            "development";
#endif
        cfg.tracesSampleRate =
#ifdef NDEBUG
            0.20;
#else
            1.0;
#endif
        if (const char* env = std::getenv("MIB_SENTRY_DSN")) {
            cfg.dsn = env;
        }
        if (const char* envEnv = std::getenv("MIB_CRASH_ENV")) {
            cfg.environment = envEnv;
        }
        if (const char* traceRate = std::getenv("MIB_SENTRY_TRACES_SAMPLE_RATE")) {
            char* end = nullptr;
            const double parsed = std::strtod(traceRate, &end);
            if (end != traceRate) {
                cfg.tracesSampleRate = std::max(0.0, std::min(1.0, parsed));
            }
        }

        backend::services::CrashReporter::init(cfg);

        // Register the state mirror as the source of crash-time JSON.
        backend::services::CrashReporter::registerStateMirror([]() {
            return backend::diagnostics::CrashStateMirror::instance().snapshotJsonString();
        });

        // Seed initial app context.
        auto& mirror = backend::diagnostics::CrashStateMirror::instance();
        mirror.setDataDir(dataDir);
        mirror.setBuildVersion(MIB_STUDIO_QT_VERSION);

        backend::services::CrashReporter::setTag("os", QSysInfo::prettyProductName().toStdString());
        backend::services::CrashReporter::setTag("kernel", QSysInfo::kernelVersion().toStdString());
        backend::services::CrashReporter::setTag("release", cfg.release);
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

        // Apply GUI-persisted startup disable list unless env already specifies it.
        applyBootDisabledServicesFromSettings();
        
        // Get executable directory and resolve data path
        QString exeDir = QCoreApplication::applicationDirPath();
        QString dataDir = QDir(exeDir).filePath("data");

        // Convert to std::string for backend
        std::string dataDirStd = dataDir.toStdString();

        // Install crash reporter BEFORE Logger / AppBackend so that crashes
        // during backend init are still captured. Logger init happens inside
        // AppBackend::initialize() and CrashReporter uses spdlog for its own
        // diagnostic messages once Logger comes online.
        installCrashReporter(exeDir, dataDirStd);

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
        applyStartupWindowGeometry(w);
        w.show();
        
        std::cout << "Application started successfully." << std::endl;

        const int rc = app.exec();
        backend::services::CrashReporter::shutdown();
        return rc;

    } catch (const std::exception& e) {
        std::string errorMsg = std::string("Unhandled exception: ") + e.what();
        writeEarlyError(errorMsg);
        backend::services::CrashReporter::captureException(e.what());
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
        backend::services::CrashReporter::captureException("unknown exception at top level");
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

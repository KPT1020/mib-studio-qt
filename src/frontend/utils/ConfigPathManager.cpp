#include "frontend/utils/ConfigPathManager.h"

#include <QCoreApplication>
#include <QDir>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#endif

namespace frontend
{

    QString ConfigPathManager::getUserConfigDirectory()
    {
        QString appDir = QCoreApplication::applicationDirPath();
        QString appDirLower = appDir.toLower();

#ifdef _WIN32
        // Check if installed in Program Files (requires admin to write)
        if (appDirLower.contains("program files") ||
            appDirLower.contains("program files (x86)"))
        {
            // Use user-writable location
            char appDataPath[MAX_PATH];
            if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appDataPath)))
            {
                QString userConfigDir = QDir(QString::fromStdString(std::string(appDataPath) + "\\MIB_Studio_Qt\\include")).absolutePath();
                // Ensure directory exists
                QDir().mkpath(userConfigDir);
                return userConfigDir;
            }
        }
#endif
        // Development: use ../include/ relative to executable
        return QDir(appDir).absoluteFilePath("../include");
    }

    QString ConfigPathManager::getIncludePath(const QString &fileName)
    {
        return QDir(getUserConfigDirectory()).absoluteFilePath(fileName);
    }

    QString ConfigPathManager::getConfigPath()
    {
        return getIncludePath("config.json");
    }

    QString ConfigPathManager::getProfilesPath()
    {
        return QDir(getUserConfigDirectory()).absoluteFilePath("profiles");
    }

} // namespace frontend

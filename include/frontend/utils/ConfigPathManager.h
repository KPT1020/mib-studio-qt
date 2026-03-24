#pragma once

#include <QString>
#include <QDir>

namespace frontend
{

    class ConfigPathManager
    {
    public:
        // Get user-writable config directory, falling back to ../include/ for development
        static QString getUserConfigDirectory();

        // Get path to a file in the config include directory
        static QString getIncludePath(const QString &fileName);

        // Get path to config.json file
        static QString getConfigPath();

        // Get path to profiles directory
        static QString getProfilesPath();

    private:
        ConfigPathManager() = default; // Static utility class
    };

} // namespace frontend

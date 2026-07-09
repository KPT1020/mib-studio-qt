#include "frontend/system/DefaultConfigTrustGate.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStringList>
#include <QtGlobal>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#endif

#include <string>

namespace frontend {
namespace {

constexpr const char* kExternalConfigKey = "Config/ExternalAppConfigPath";
constexpr const char* kLastProfileKey = "Profiles/LastProfileName";
constexpr const char* kConfirmedHashesKey = "Config/ConfirmedDefaultConfigHashes";

QString userConfigDir()
{
    QString appDir = QCoreApplication::applicationDirPath();
    const QString appDirLower = appDir.toLower();

#ifdef _WIN32
    if (appDirLower.contains("program files") || appDirLower.contains("program files (x86)")) {
        char appDataPath[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appDataPath))) {
            const QString userDir = QDir(QString::fromStdString(std::string(appDataPath) + "\\MIB_Studio_Qt\\include")).absolutePath();
            QDir().mkpath(userDir);
            return userDir;
        }
    }
#else
    Q_UNUSED(appDirLower);
#endif

    return QDir(appDir).absoluteFilePath("../include");
}

QString fileSha256Hex(const QString& path, QString* errorOut)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorOut) {
            *errorOut = file.errorString();
        }
        return {};
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        if (errorOut) {
            *errorOut = QStringLiteral("Failed to read config for hashing");
        }
        return {};
    }
    return QString::fromLatin1(hash.result().toHex());
}

QStringList confirmedHashes()
{
    return QSettings().value(kConfirmedHashesKey).toStringList();
}

bool samePath(const QString& left, const QString& right)
{
    return QFileInfo(left).absoluteFilePath() == QFileInfo(right).absoluteFilePath();
}

} // namespace

QString DefaultConfigTrustGate::defaultConfigPath()
{
    const QByteArray overridePath = qgetenv("MIB_DEFAULT_CONFIG_PATH_FOR_TESTS");
    if (!overridePath.isEmpty()) {
        return QFileInfo(QString::fromLocal8Bit(overridePath)).absoluteFilePath();
    }
    return QDir(userConfigDir()).absoluteFilePath("config.json");
}

QString DefaultConfigTrustGate::activeConfigPath()
{
    QSettings settings;
    const QString external = settings.value(kExternalConfigKey).toString().trimmed();
    if (!external.isEmpty()) {
        return external;
    }
    return defaultConfigPath();
}

QString DefaultConfigTrustGate::defaultConfigHash(QString* errorOut)
{
    return fileSha256Hex(defaultConfigPath(), errorOut);
}

DefaultConfigTrustGate::State DefaultConfigTrustGate::state() const
{
    QSettings settings;
    const QString external = settings.value(kExternalConfigKey).toString().trimmed();
    const QString profile = settings.value(kLastProfileKey).toString().trimmed();

    State out;
    out.hasExternalConfig = !external.isEmpty();
    out.hasActiveProfile = !profile.isEmpty();
    out.defaultConfigPath = defaultConfigPath();
    out.activeConfigPath = out.hasExternalConfig ? external : out.defaultConfigPath;
    out.usingDefaultConfig = !out.hasExternalConfig && !out.hasActiveProfile &&
                             samePath(out.activeConfigPath, out.defaultConfigPath);

    if (out.usingDefaultConfig) {
        out.activeDefaultHash = defaultConfigHash();
        out.defaultHashConfirmed = !out.activeDefaultHash.isEmpty() &&
                                   confirmedHashes().contains(out.activeDefaultHash);
    }
    return out;
}

bool DefaultConfigTrustGate::isProductionActionAllowed(ProductionAction action, QString* messageOut) const
{
    const State current = state();
    if (current.trustedForProduction()) {
        return true;
    }
    if (messageOut) {
        *messageOut = blockMessage(action);
    }
    return false;
}

bool DefaultConfigTrustGate::confirmActiveDefault(QString* hashOut, QString* errorOut) const
{
    const State current = state();
    if (!current.usingDefaultConfig) {
        if (errorOut) {
            *errorOut = QStringLiteral("The active config is not the bundled default config.");
        }
        return false;
    }

    QString hashError;
    const QString hash = defaultConfigHash(&hashError);
    if (hash.isEmpty()) {
        if (errorOut) {
            *errorOut = hashError.isEmpty() ? QStringLiteral("Unable to hash the active default config.") : hashError;
        }
        return false;
    }

    QStringList hashes = confirmedHashes();
    if (!hashes.contains(hash)) {
        hashes.append(hash);
        hashes.removeDuplicates();
        QSettings settings;
        settings.setValue(kConfirmedHashesKey, hashes);
        settings.sync();
    }

    if (hashOut) {
        *hashOut = hash;
    }
    return true;
}

QString DefaultConfigTrustGate::blockMessage(ProductionAction action)
{
    switch (action) {
    case ProductionAction::ExperimentStart:
        return QStringLiteral("Using default config. Review and confirm the displayed default config, or load a profile or external config, before starting an experiment.");
    case ProductionAction::FrameRecordingStart:
        return QStringLiteral("Using default config. Review and confirm the displayed default config, or load a profile or external config, before starting frame recording.");
    case ProductionAction::CameraApply:
        return QStringLiteral("Using default config. Review and confirm the displayed default config, or load a profile or external config, before applying camera settings.");
    }
    return QStringLiteral("Using default config. Review and confirm the displayed default config, or load a profile or external config, before continuing.");
}

} // namespace frontend

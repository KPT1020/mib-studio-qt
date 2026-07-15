#include "frontend/utils/ApplicationSettings.h"

#include <QCoreApplication>
#include <QSettings>

namespace frontend::applicationsettings {
namespace {

constexpr auto kOrganizationName = "MIB Studio";
constexpr auto kOrganizationDomain = "yofo.bio";
constexpr auto kApplicationName = "MIB Studio Qt";
constexpr auto kLegacyOrganizationName = "Unknown Organization";
constexpr auto kLegacyMigrationMarker = "SettingsMigration/UnknownOrganizationV1";

bool reportSyncFailure(const QSettings& settings, QString* error) {
    if (error) {
        *error = QStringLiteral("Could not migrate legacy application settings to %1 (status %2).")
                     .arg(settings.fileName())
                     .arg(static_cast<int>(settings.status()));
    }
    return false;
}

} // namespace

bool initialize(QString* error) {
    QCoreApplication::setOrganizationName(QString::fromLatin1(kOrganizationName));
    QCoreApplication::setOrganizationDomain(QString::fromLatin1(kOrganizationDomain));
    QCoreApplication::setApplicationName(QString::fromLatin1(kApplicationName));

    QSettings current;
    QSettings legacy(QSettings::defaultFormat(), QSettings::UserScope,
                     QString::fromLatin1(kLegacyOrganizationName),
                     QString::fromLatin1(kApplicationName));
    if (current.fileName() == legacy.fileName()) return true;
    if (current.value(QString::fromLatin1(kLegacyMigrationMarker), false).toBool()) return true;

    bool copied = false;
    for (const QString& key : legacy.allKeys()) {
        if (current.contains(key)) continue;
        current.setValue(key, legacy.value(key));
        copied = true;
    }
    if (copied) {
        current.sync();
        if (current.status() != QSettings::NoError) return reportSyncFailure(current, error);
    }

    // Record completion only after the copied settings themselves have
    // synchronized successfully. This prevents a later startup from
    // resurrecting a legacy key that the user intentionally removed.
    current.setValue(QString::fromLatin1(kLegacyMigrationMarker), true);
    current.sync();
    if (current.status() == QSettings::NoError) return true;
    current.remove(QString::fromLatin1(kLegacyMigrationMarker));
    return reportSyncFailure(current, error);
}

} // namespace frontend::applicationsettings

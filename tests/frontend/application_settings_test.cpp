#include "frontend/utils/ApplicationSettings.h"

#include "support/assert.h"
#include "support/tempdir.h"

#include <QCoreApplication>
#include <QSettings>

#include <filesystem>
#include <fstream>

namespace {

constexpr auto kApplicationName = "MIB Studio Qt";
constexpr auto kOrganizationName = "MIB Studio";
constexpr auto kLegacyOrganizationName = "Unknown Organization";
constexpr auto kMigrationMarker = "SettingsMigration/UnknownOrganizationV1";

QSettings explicitSettings(const char* organization) {
    return QSettings(QSettings::IniFormat, QSettings::UserScope, QString::fromLatin1(organization),
                     QString::fromLatin1(kApplicationName));
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QSettings::setDefaultFormat(QSettings::IniFormat);

    mib::test::TempDir successRoot("application_settings_migration");
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       QString::fromStdString(successRoot.path().string()));
    {
        auto legacy = explicitSettings(kLegacyOrganizationName);
        legacy.setValue(QStringLiteral("Startup/DisabledServices"), QStringLiteral("trigger"));
        legacy.setValue(QStringLiteral("Update/Channel"), QStringLiteral("stable"));
        legacy.setValue(QStringLiteral("ProcessingCore/Version"), QStringLiteral("1.2.3"));
        legacy.setValue(QStringLiteral("Arbitrary/NestedPreference"), 42);
        legacy.sync();
        MIB_REQUIRE(legacy.status() == QSettings::NoError, "legacy fixture settings are writable");
    }
    {
        auto current = explicitSettings(kOrganizationName);
        current.setValue(QStringLiteral("Update/Channel"), QStringLiteral("beta"));
        current.sync();
        MIB_REQUIRE(current.status() == QSettings::NoError, "stable settings fixture is writable");
    }

    QString error;
    MIB_REQUIRE(frontend::applicationsettings::initialize(&error), error.toStdString());
    MIB_EXPECT(QCoreApplication::organizationName() == QStringLiteral("MIB Studio"),
               "stable organization name is configured");
    MIB_EXPECT(QCoreApplication::organizationDomain() == QStringLiteral("yofo.bio"),
               "stable organization domain is configured");
    MIB_EXPECT(QCoreApplication::applicationName() == QStringLiteral("MIB Studio Qt"),
               "stable application name is configured");
    {
        QSettings current;
        MIB_EXPECT(current.value(QStringLiteral("Startup/DisabledServices")).toString() ==
                       QStringLiteral("trigger"),
                   "missing startup preference migrates from legacy namespace");
        MIB_EXPECT(current.value(QStringLiteral("ProcessingCore/Version")).toString() ==
                       QStringLiteral("1.2.3"),
                   "processing-core selection migrates with all other settings");
        MIB_EXPECT(current.value(QStringLiteral("Arbitrary/NestedPreference")).toInt() == 42,
                   "migration copies arbitrary legacy keys, not a hand-picked subset");
        MIB_EXPECT(current.value(QStringLiteral("Update/Channel")).toString() ==
                       QStringLiteral("beta"),
                   "existing stable setting wins over conflicting legacy value");
        MIB_EXPECT(current.value(QString::fromLatin1(kMigrationMarker)).toBool(),
                   "successful migration records completion");

        current.remove(QStringLiteral("ProcessingCore/Version"));
        current.sync();
        MIB_REQUIRE(current.status() == QSettings::NoError,
                    "deliberate removal from stable settings succeeds");
    }
    MIB_REQUIRE(frontend::applicationsettings::initialize(&error), error.toStdString());
    {
        QSettings current;
        MIB_EXPECT(!current.contains(QStringLiteral("ProcessingCore/Version")),
                   "completed migration does not resurrect a deliberately removed key");
    }
    {
        auto legacy = explicitSettings(kLegacyOrganizationName);
        MIB_EXPECT(legacy.value(QStringLiteral("ProcessingCore/Version")).toString() ==
                       QStringLiteral("1.2.3"),
                   "migration never deletes the legacy namespace");
    }

    mib::test::TempDir failureRoot("application_settings_migration_failure");
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       QString::fromStdString(failureRoot.path().string()));
    {
        auto legacy = explicitSettings(kLegacyOrganizationName);
        legacy.setValue(QStringLiteral("ProcessingCore/Version"), QStringLiteral("9.9.9"));
        legacy.sync();
        MIB_REQUIRE(legacy.status() == QSettings::NoError,
                    "failure-case legacy fixture is writable");
    }
    const auto stableParent = failureRoot.path() / kOrganizationName;
    {
        std::ofstream blocker(stableParent);
        MIB_REQUIRE(blocker.good(), "create a file where the stable settings directory belongs");
    }
    error.clear();
    MIB_EXPECT(!frontend::applicationsettings::initialize(&error),
               "settings migration reports a deterministic parent-path write failure");
    MIB_EXPECT(!error.isEmpty(), "settings migration failure includes a diagnostic");
    {
        auto current = explicitSettings(kOrganizationName);
        MIB_EXPECT(!current.value(QString::fromLatin1(kMigrationMarker), false).toBool(),
                   "failed migration has no completion marker");
    }

    return mib::test::exitCode();
}

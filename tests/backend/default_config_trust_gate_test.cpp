#include "frontend/system/DefaultConfigTrustGate.h"

#include "support/assert.h"
#include "support/tempdir.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QtGlobal>

#include <filesystem>
#include <fstream>

namespace
{
void writeText(const std::filesystem::path& path, const char* text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

void resetSettings()
{
    QSettings settings;
    settings.clear();
    settings.sync();
}
} // namespace

int main(int argc, char* argv[])
{
    mib::test::TempDir xdg("mib_default_gate_xdg");
    qputenv("XDG_CONFIG_HOME", QByteArray::fromStdString(xdg.path().string()));
    qputenv("MIB_DEFAULT_CONFIG_PATH_FOR_TESTS", QByteArray::fromStdString((xdg.path() / "default_config.json").string()));

    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("MIBStudioQtTests"));
    QCoreApplication::setApplicationName(QStringLiteral("default_config_trust_gate"));
    resetSettings();

    frontend::DefaultConfigTrustGate gate;
    const auto defaultPath = std::filesystem::path(frontend::DefaultConfigTrustGate::defaultConfigPath().toStdString());
    writeText(defaultPath, "{\"Camera\":{\"Exposure\":10}}\n");

    auto state = gate.state();
    MIB_EXPECT(state.usingDefaultConfig, "fresh default state uses bundled default config path");
    MIB_EXPECT(!state.defaultHashConfirmed, "fresh default hash is not confirmed");
    MIB_EXPECT(!state.trustedForProduction(), "unconfirmed default is not trusted");
    QString message;
    MIB_EXPECT(!gate.isProductionActionAllowed(frontend::DefaultConfigTrustGate::ProductionAction::ExperimentStart, &message),
               "experiment start is blocked on unconfirmed default");
    MIB_EXPECT(message.contains(QStringLiteral("Using default config")), "block message names default config state");

    QString originalHash;
    MIB_EXPECT(gate.confirmActiveDefault(&originalHash), "confirm active default succeeds");
    state = gate.state();
    MIB_EXPECT(state.defaultHashConfirmed, "confirmed default hash is trusted");
    MIB_EXPECT(gate.isProductionActionAllowed(frontend::DefaultConfigTrustGate::ProductionAction::FrameRecordingStart),
               "confirmed default permits recording");

    writeText(defaultPath, "{\"Camera\":{\"Exposure\":20}}\n");
    state = gate.state();
    MIB_EXPECT(state.usingDefaultConfig, "changed default remains the active default state");
    MIB_EXPECT(state.activeDefaultHash != originalHash, "changed default produces a different hash");
    MIB_EXPECT(!state.defaultHashConfirmed, "changed default requires review again");

    const auto externalPath = xdg.path() / "external_config.json";
    writeText(externalPath, "{\"external\":true}\n");
    {
        QSettings settings;
        settings.setValue(QStringLiteral("Config/ExternalAppConfigPath"), QString::fromStdString(externalPath.string()));
        settings.sync();
    }
    state = gate.state();
    MIB_EXPECT(!state.usingDefaultConfig, "external config clears default state");
    MIB_EXPECT(state.trustedForProduction(), "external config is trusted automatically");

    {
        QSettings settings;
        settings.remove(QStringLiteral("Config/ExternalAppConfigPath"));
        settings.setValue(QStringLiteral("Profiles/LastProfileName"), QStringLiteral("profile-a"));
        settings.sync();
    }
    state = gate.state();
    MIB_EXPECT(!state.usingDefaultConfig, "active profile clears default state");
    MIB_EXPECT(state.trustedForProduction(), "active profile is trusted automatically");

    {
        QSettings settings;
        settings.remove(QStringLiteral("Profiles/LastProfileName"));
        settings.sync();
    }
    state = gate.state();
    MIB_EXPECT(state.usingDefaultConfig, "clearing profile returns to default state");
    MIB_EXPECT(!state.defaultHashConfirmed, "changed default is still unconfirmed after clear-back");

    writeText(defaultPath, "{\"Camera\":{\"Exposure\":10}}\n");
    state = gate.state();
    MIB_EXPECT(state.usingDefaultConfig, "original default hash is active again");
    MIB_EXPECT(state.defaultHashConfirmed, "clear-back to a previously confirmed hash stays trusted");

    resetSettings();
    return mib::test::exitCode();
}

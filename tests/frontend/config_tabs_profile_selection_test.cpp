#define private public
#include "frontend/tabs/ConfigTabs.h"
#undef private

#include "backend/app/AppBackend.h"
#include "support/assert.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>

#include <cstdio>

namespace {

bool writeTextFile(const QString& path, const QByteArray& content)
{
    QFileInfo info(path);
    QDir().mkpath(info.absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(content) == content.size();
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("MIBStudioQtTests"));
    QCoreApplication::setApplicationName(QStringLiteral("ConfigTabsProfileSelection"));

    const QString profileName = QStringLiteral("active-config-tabs-test");
    const QString fallbackConfig = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../include/config.json"));
    const QString fallbackScript = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../include/egrabberConfig.js"));
    const QString activeConfig = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../include/profiles/%1/config.json").arg(profileName));
    const QString activeScript = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../include/profiles/%1/egrabberConfig.js").arg(profileName));

    MIB_REQUIRE(writeTextFile(activeConfig, QByteArrayLiteral("{\"target_fps\": 120}\n")), "active profile config is written");
    MIB_REQUIRE(writeTextFile(activeScript, QByteArrayLiteral("// active profile script\n")), "active profile script is written");
    MIB_REQUIRE(writeTextFile(fallbackConfig, QByteArrayLiteral("{\"target_fps\": 60}\n")), "fallback config is written");
    MIB_REQUIRE(writeTextFile(fallbackScript, QByteArrayLiteral("// fallback script\n")), "fallback script is written");

    QSettings settings;
    settings.clear();
    settings.setValue(QStringLiteral("Profiles/LastProfileName"), profileName);
    settings.setValue(QStringLiteral("Config/ExternalAppConfigPath"), activeConfig);
    settings.setValue(QStringLiteral("Config/ExternalCameraScriptPath"), activeScript);
    settings.sync();

    backend::AppBackend backend;
    frontend::ConfigTabs tabs(backend);

    MIB_REQUIRE(tabs.profileSelect_ != nullptr, "profile selector exists");
    const int activeIndex = tabs.profileSelect_->findData(profileName);
    MIB_REQUIRE(activeIndex > 0, "active profile appears in selector");
    MIB_EXPECT(tabs.profileSelect_->currentIndex() == activeIndex, "active profile is selected from settings");

    QSignalSpy pathChanged(&tabs, &frontend::ConfigTabs::appConfigPathChanged);
    tabs.profileSelect_->setCurrentIndex(0);
    QCoreApplication::processEvents();

    QSettings after;
    MIB_EXPECT(after.value(QStringLiteral("Profiles/LastProfileName")).toString().isEmpty(), "last profile is cleared");
    MIB_EXPECT(after.value(QStringLiteral("Config/ExternalAppConfigPath")).toString().isEmpty(), "external app config path is cleared");
    MIB_EXPECT(after.value(QStringLiteral("Config/ExternalCameraScriptPath")).toString().isEmpty(), "external camera script path is cleared");
    MIB_EXPECT(QFileInfo(tabs.currentJsonPath()).absoluteFilePath() == QFileInfo(fallbackConfig).absoluteFilePath(),
               "current config path reverts to default");
    MIB_EXPECT(QFileInfo(tabs.currentJsPath()).absoluteFilePath() == QFileInfo(fallbackScript).absoluteFilePath(),
               "current script path reverts to default");
    MIB_EXPECT(!pathChanged.isEmpty(), "config path change is emitted");
    MIB_EXPECT(tabs.profileReviewRows_.isEmpty(), "review rows are cleared with no profile selected");
    MIB_EXPECT(tabs.loadProfileBtn_ && !tabs.loadProfileBtn_->isEnabled(), "load profile button is disabled");

    if (mib::test::exitCode() == 0) {
        std::printf("ConfigTabs no-profile selection clears active profile settings\n");
    }
    return mib::test::exitCode();
}

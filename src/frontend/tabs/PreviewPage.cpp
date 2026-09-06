#include "frontend/tabs/PreviewPage.h"
#include "ui_PreviewPage.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QSettings>

#include <spdlog/spdlog.h>

#include "backend/app/AppBackend.h"
#include "frontend/system/PlaybackPanel.h"
#include "frontend/tabs/ConfigTabs.h"
#include "frontend/system/AppConfigWatcher.h"

namespace frontend
{

    PreviewPage::PreviewPage(backend::AppBackend &backend, QWidget *parent)
        : QWidget(parent), ui(new Ui::PreviewPage), backend_(backend)
    {
        ui->setupUi(this);

        // Live image container hosts the PlaybackPanel directly (issue #360):
        // there is no acquisition overlay covering the image any more. Camera
        // start/stop lives in the application chrome through the single
        // CameraController command path owned by MainWindow.
        auto *imageLayout = new QVBoxLayout(ui->overlayContainer);
        imageLayout->setContentsMargins(0, 0, 0, 0);
        imageLayout->setSpacing(0);
        playback_ = new PlaybackPanel(backend_, ui->overlayContainer);
        playback_->setObjectName(QStringLiteral("previewPlaybackPanel"));
        imageLayout->addWidget(playback_);

        // Bottom: configuration tabs
        configTabs_ = new ConfigTabs(backend_, this);
        // Allow the splitter to shrink this area aggressively (Qt layouts otherwise enforce a large minimum)
        configTabs_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
        configTabs_->setMinimumHeight(0);

        // Replace placeholder with actual ConfigTabs widget
        ui->splitter->replaceWidget(1, configTabs_);

        // Set initial proportions (50/50 ratio)
        ui->splitter->setStretchFactor(0, 1);
        ui->splitter->setStretchFactor(1, 1);

        // Set explicit sizes for 50/50 split after widget is shown
        QTimer::singleShot(0, this, [this]() {
            int height = ui->splitter->height();
            if (height > 0) {
                int half = height / 2;
                ui->splitter->setSizes({half, half});
            }
        });

        // Live config watcher: watch current path and apply changes to services and playback
        configWatcher_ = new AppConfigWatcher(backend_, playback_, this);
        connect(configTabs_, &ConfigTabs::appConfigPathChanged, configWatcher_, &AppConfigWatcher::setWatchedPath);
        // Connect file change signal to ConfigTabs to refresh JSON editor and table
        connect(configWatcher_, &AppConfigWatcher::configFileChanged, configTabs_, &ConfigTabs::onExternalConfigFileChanged);
        configWatcher_->start();
    }

    PreviewPage::~PreviewPage() {
        delete ui;
    }

} // namespace frontend

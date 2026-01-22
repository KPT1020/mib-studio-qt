#include "frontend/tabs/PreviewPage.h"
#include "ui_PreviewPage.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QStackedLayout>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QSettings>

#include <spdlog/spdlog.h>

#include "backend/AppBackend.h"
#include "backend/services/CaptureService.h"
#include "frontend/system/PlaybackPanel.h"
#include "frontend/tabs/ConfigTabs.h"
#include "frontend/system/AppConfigWatcher.h"

namespace frontend
{

    PreviewPage::PreviewPage(backend::AppBackend &backend, QWidget *parent)
        : QWidget(parent), ui(new Ui::PreviewPage), backend_(backend)
    {
        ui->setupUi(this);

        setupOverlayWidget();

        // Bottom: configuration tabs
        configTabs_ = new ConfigTabs(backend_, this);
        // Allow the splitter to shrink this area aggressively (Qt layouts otherwise enforce a large minimum)
        configTabs_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
        configTabs_->setMinimumHeight(0);
        
        // Replace placeholder with actual ConfigTabs widget
        ui->splitter->replaceWidget(1, configTabs_);
        
        // Set initial proportions (3:2 ratio)
        ui->splitter->setStretchFactor(0, 3);
        ui->splitter->setStretchFactor(1, 2);

        // Live config watcher: watch current path and apply changes to services and playback
        configWatcher_ = new AppConfigWatcher(backend_, playback_, this);
        connect(configTabs_, &ConfigTabs::appConfigPathChanged, configWatcher_, &AppConfigWatcher::setWatchedPath);
        // Connect file change signal to ConfigTabs to refresh JSON editor and table
        connect(configWatcher_, &AppConfigWatcher::configFileChanged, configTabs_, &ConfigTabs::onExternalConfigFileChanged);
        configWatcher_->start();

        onUpdateOverlay();
    }

    PreviewPage::~PreviewPage() {
        delete ui;
    }

    void PreviewPage::setupOverlayWidget() {
        // Top: Playback panel with centered Play/Stop overlay
        auto *stacked = new QStackedLayout(ui->overlayContainer);
        stacked->setStackingMode(QStackedLayout::StackAll);

        playback_ = new PlaybackPanel(backend_, ui->overlayContainer);
        stacked->addWidget(playback_);

        QWidget *overlay = new QWidget(ui->overlayContainer);
        auto *overlayLayout = new QHBoxLayout(overlay);
        overlayLayout->setAlignment(Qt::AlignCenter);

        playBtn_ = new QToolButton(overlay);
        playBtn_->setText(tr("▶ Play"));
        playBtn_->setToolButtonStyle(Qt::ToolButtonTextOnly);
        playBtn_->setAutoRaise(true);
        playBtn_->setMinimumSize(120, 48);

        stopBtn_ = new QToolButton(overlay);
        stopBtn_->setText(tr("■ Stop"));
        stopBtn_->setToolButtonStyle(Qt::ToolButtonTextOnly);
        stopBtn_->setAutoRaise(true);
        stopBtn_->setMinimumSize(120, 48);

        overlayLayout->addWidget(playBtn_);
        overlayLayout->addSpacing(12);
        overlayLayout->addWidget(stopBtn_);

        stacked->addWidget(overlay);

        connect(playBtn_, &QToolButton::clicked, this, &PreviewPage::onPlay);
        connect(stopBtn_, &QToolButton::clicked, this, &PreviewPage::onStop);

        // Periodically update overlay visibility
        auto *timer = new QTimer(this);
        timer->setInterval(300);
        connect(timer, &QTimer::timeout, this, &PreviewPage::onUpdateOverlay);
        timer->start();
    }

    void PreviewPage::onPlay()
    {
        auto &cap = backend_.capture();
        if (!cap.isRunning())
        {
            SPDLOG_INFO("PreviewPage: starting capture");
            cap.start();
        }
        onUpdateOverlay();
    }

    void PreviewPage::onStop()
    {
        auto &cap = backend_.capture();
        if (cap.isRunning())
        {
            SPDLOG_INFO("PreviewPage: stopping capture");
            cap.stop();
        }
        onUpdateOverlay();
    }

    void PreviewPage::onUpdateOverlay()
    {
        const bool running = backend_.capture().isRunning();
        playBtn_->setEnabled(!running);
        stopBtn_->setEnabled(running);
    }

} // namespace frontend

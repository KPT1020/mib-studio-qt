#include "frontend/PreviewPage.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QStackedLayout>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QSettings>

#include <spdlog/spdlog.h>

#include "backend/AppBackend.h"
#include "backend/services/CaptureService.h"
#include "frontend/PlaybackPanel.h"
#include "frontend/ConfigTabs.h"
#include "frontend/AppConfigWatcher.h"

namespace frontend
{

    PreviewPage::PreviewPage(backend::AppBackend &backend, QWidget *parent)
        : QWidget(parent), backend_(backend)
    {
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(6);

        // Top: Playback panel with centered Play/Stop overlay
        QWidget *overlayContainer = new QWidget(this);
        overlayContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

        auto *stacked = new QStackedLayout(overlayContainer);
        stacked->setStackingMode(QStackedLayout::StackAll);

        playback_ = new PlaybackPanel(backend_, overlayContainer);
        stacked->addWidget(playback_);

        QWidget *overlay = new QWidget(overlayContainer);
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

        // Bottom: configuration tabs
        configTabs_ = new ConfigTabs(backend_, this);

        root->addWidget(overlayContainer, 3);
        root->addWidget(configTabs_, 2);

        // Live config watcher: watch current path and apply changes to services and playback
        configWatcher_ = new AppConfigWatcher(backend_, playback_, this);
        connect(configTabs_, &ConfigTabs::appConfigPathChanged, configWatcher_, &AppConfigWatcher::setWatchedPath);
        // Connect file change signal to ConfigTabs to refresh JSON editor and table
        connect(configWatcher_, &AppConfigWatcher::configFileChanged, configTabs_, &ConfigTabs::onExternalConfigFileChanged);
        configWatcher_->start();

        onUpdateOverlay();
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

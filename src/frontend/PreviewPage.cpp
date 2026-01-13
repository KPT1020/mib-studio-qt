#include "frontend/PreviewPage.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QStackedLayout>
#include <QSplitter>
#include <QSplitterHandle>
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
        root->setSpacing(0);

        // Create vertical splitter for resizable top/bottom sections
        QSplitter *splitter = new QSplitter(Qt::Vertical, this);
        splitter->setChildrenCollapsible(false);
        splitter->setHandleWidth(10);
        splitter->setOpaqueResize(true);

        // Top: Playback panel with centered Play/Stop overlay
        QWidget *overlayContainer = new QWidget(this);
        overlayContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        overlayContainer->setMinimumHeight(100);

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
        // Allow the splitter to shrink this area aggressively (Qt layouts otherwise enforce a large minimum)
        configTabs_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
        configTabs_->setMinimumHeight(0);

        // Add widgets to splitter
        splitter->addWidget(overlayContainer);
        splitter->addWidget(configTabs_);
        
        // Set initial proportions (3:2 ratio)
        splitter->setStretchFactor(0, 3);
        splitter->setStretchFactor(1, 2);

        root->addWidget(splitter);

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

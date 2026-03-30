#include "frontend/tabs/PreviewPage.h"
#include "ui_PreviewPage.h"

#include <QHBoxLayout>
#include <QStackedLayout>
#include <QTimer>
#include <QToolButton>

#include <spdlog/spdlog.h>

#include "backend/AppBackend.h"
#include "backend/services/CaptureService.h"
#include "frontend/system/PlaybackPanel.h"

namespace frontend
{

    PreviewPage::PreviewPage(backend::AppBackend &backend, QWidget *parent)
        : QWidget(parent), ui(new Ui::PreviewPage), backend_(backend)
    {
        ui->setupUi(this);
        setupOverlayWidget();
        onUpdateOverlay();
    }

    PreviewPage::~PreviewPage() {
        delete ui;
    }

    void PreviewPage::setupOverlayWidget() {
        // PlaybackPanel with centered Play/Stop overlay
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

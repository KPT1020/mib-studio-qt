#include "frontend/MainWindow.h"

#include <QAction>
#include <QLabel>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QTabWidget>

#include "backend/AppBackend.h"
#include "backend/services/CaptureService.h"
#include "frontend/PlaybackPanel.h"
#include "frontend/ConnectTab.h"
#include "frontend/PreviewPage.h"

MainWindow::MainWindow(backend::AppBackend& backend, QWidget* parent)
    : QMainWindow(parent), backend_(backend) {
    setWindowTitle("MIB Studio Qt Scaffold");

    auto* toolbar = addToolBar("Capture");
    auto* startAct = new QAction("Start", this);
    auto* stopAct = new QAction("Stop", this);
    toolbar->addAction(startAct);
    toolbar->addAction(stopAct);

    connect(startAct, &QAction::triggered, this, &MainWindow::onStartCapture);
    connect(stopAct, &QAction::triggered, this, &MainWindow::onStopCapture);

    statusLabel_ = new QLabel("Idle");
    statusBar()->addPermanentWidget(statusLabel_);

    statsTimer_ = new QTimer(this);
    statsTimer_->setInterval(500);
    connect(statsTimer_, &QTimer::timeout, this, &MainWindow::onUpdateStats);

    // Tabs: Connect + Preview
    tabs_ = new QTabWidget(this);
    auto* connectTab = new frontend::ConnectTab(backend_, tabs_);
    auto* previewPage = new frontend::PreviewPage(backend_, tabs_);
    tabs_->addTab(connectTab, tr("Connect"));
    tabs_->addTab(previewPage, tr("Preview"));
    setCentralWidget(tabs_);

    connect(connectTab, &frontend::ConnectTab::connected, this, [this]() {
        if (tabs_) tabs_->setCurrentIndex(1);
    });
}

void MainWindow::onStartCapture() {
    auto& cap = backend_.capture();
    if (!cap.isRunning()) {
        cap.start();
        statsTimer_->start();
    }
}

void MainWindow::onStopCapture() {
    auto& cap = backend_.capture();
    if (cap.isRunning()) {
        cap.stop();
        statsTimer_->stop();
        statusLabel_->setText("Stopped");
    }
}

void MainWindow::onUpdateStats() {
    const auto& s = backend_.capture().stats();
    statusLabel_->setText(QString("frames=%1, fps=%2, MB/s=%3")
                              .arg(QString::number(s.framesProcessed.load()))
                              .arg(QString::number(s.lastFrameRate.load()))
                              .arg(QString::number(s.lastDataRateMBps.load())));
}

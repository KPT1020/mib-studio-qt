#include "frontend/MainWindow.h"

#include <QAction>
#include <QLabel>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

#include "backend/AppBackend.h"
#include "backend/services/CaptureService.h"

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

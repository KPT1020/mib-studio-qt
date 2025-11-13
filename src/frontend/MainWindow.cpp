#include "frontend/MainWindow.h"

#include <QAction>
#include <QLabel>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QTabWidget>
#include <QFileDialog>
#include <QMessageBox>

#include "backend/AppBackend.h"
#include "backend/services/CaptureService.h"
#include "backend/services/ProcessingService.h"
#include "backend/services/Hdf5Service.h"
#include "frontend/PlaybackPanel.h"
#include "frontend/ConnectTab.h"
#include "frontend/PreviewPage.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QSpinBox>
#include <QLayout>
#include <QThread>
#include <thread>
#include <QMenuBar>
#include <QMenu>
#include "frontend/ProcessingSettingsDialog.h"
#ifdef _WIN32
#include <windows.h>
#endif

MainWindow::MainWindow(backend::AppBackend &backend, QWidget *parent)
    : QMainWindow(parent), backend_(backend)
{
    setWindowTitle("MIB Studio Qt Scaffold");

    // Menu bar
    auto *fileMenu = menuBar()->addMenu(tr("File"));
    auto *exitAct = fileMenu->addAction(tr("Exit"));
    connect(exitAct, &QAction::triggered, this, &QWidget::close);

    auto *settingsMenu = menuBar()->addMenu(tr("Settings"));
    auto *processingSettingsAct = settingsMenu->addAction(tr("Processing Settings..."));
    connect(processingSettingsAct, &QAction::triggered, this, [this]()
            {
        SPDLOG_INFO("Opening Processing Settings dialog");
        ProcessingSettingsDialog dlg(backend_, this);
        dlg.exec(); });

    auto *helpMenu = menuBar()->addMenu(tr("Help"));
    auto *aboutAct = helpMenu->addAction(tr("About"));
    connect(aboutAct, &QAction::triggered, this, [this]()
            { QMessageBox::about(this,
                                 tr("About MIB Studio Qt Scaffold"),
                                 tr("MIB Studio Qt Scaffold\n\nProvides camera capture, processing, and HDF5 logging.")); });

    auto *toolbar = addToolBar("Capture");
    auto *startCaptureAct = new QAction("Start Camera", this);
    auto *stopCaptureAct = new QAction("Stop Camera", this);
    toolbar->addAction(startCaptureAct);
    toolbar->addAction(stopCaptureAct);
    toolbar->addSeparator();
    auto *startExperimentAct = new QAction("Start Experiment", this);
    auto *stopExperimentAct = new QAction("Stop Experiment", this);
    toolbar->addAction(startExperimentAct);
    toolbar->addAction(stopExperimentAct);

    // Initialize defaults for this session
    backend_.processing().setInvalidFrameSamplingRate(200);
    backend_.processing().setFlushInterval(200);

    connect(startCaptureAct, &QAction::triggered, this, &MainWindow::onStartCapture);
    connect(stopCaptureAct, &QAction::triggered, this, &MainWindow::onStopCapture);
    connect(startExperimentAct, &QAction::triggered, this, &MainWindow::onStartExperiment);
    connect(stopExperimentAct, &QAction::triggered, this, &MainWindow::onStopExperiment);

    statusLabel_ = new QLabel("Idle");
    statusBar()->addPermanentWidget(statusLabel_);

    statsTimer_ = new QTimer(this);
    statsTimer_->setInterval(500);
    connect(statsTimer_, &QTimer::timeout, this, &MainWindow::onUpdateStats);

    // Setup async flush watcher
    flushWatcher_ = new QFutureWatcher<size_t>(this);
    connect(flushWatcher_, &QFutureWatcher<size_t>::finished, this, [this]()
            {
        flushInProgress_ = false;
        size_t flushed = flushWatcher_->result();
        if (flushed > 0) {
            SPDLOG_INFO("Auto-flushed {} frames to HDF5", flushed);
        } });

    // Tabs: Connect + Preview
    tabs_ = new QTabWidget(this);
    auto *connectTab = new frontend::ConnectTab(backend_, tabs_);
    auto *previewPage = new frontend::PreviewPage(backend_, tabs_);
    tabs_->addTab(connectTab, tr("Connect"));
    tabs_->addTab(previewPage, tr("Preview"));
    setCentralWidget(tabs_);

    connect(connectTab, &frontend::ConnectTab::connected, this, [this]()
            {
        if (tabs_) tabs_->setCurrentIndex(1); });
}

void MainWindow::onStartCapture()
{
    auto &cap = backend_.capture();
    if (cap.isRunning())
    {
        return; // Already running
    }

    // Start capture only (no experiment)
    cap.start();
    statsTimer_->start();
    statusLabel_->setText("Camera running");
}

void MainWindow::onStopCapture()
{
    auto &cap = backend_.capture();
    if (!cap.isRunning())
    {
        return; // Not running
    }

    // Stop capture only (don't end experiment)
    cap.stop();
    statsTimer_->stop();

    if (experimentActive_)
    {
        statusLabel_->setText("Camera stopped (experiment still active)");
    }
    else
    {
        statusLabel_->setText("Camera stopped");
    }
}

void MainWindow::onStartExperiment()
{
    if (experimentActive_)
    {
        QMessageBox::information(this, tr("Experiment"),
                                 tr("Experiment is already running"));
        return;
    }

    // Show file dialog to select HDF5 save location
    QString filePath = QFileDialog::getSaveFileName(
        this,
        tr("Save Experiment Data"),
        "",
        tr("HDF5 Files (*.h5 *.hdf5);;All Files (*)"));

    if (filePath.isEmpty())
    {
        // User cancelled
        return;
    }

    // Convert to std::string
    std::string hdf5Path = filePath.toStdString();

    // Ensure .h5 extension
    if (hdf5Path.size() < 3 ||
        (hdf5Path.substr(hdf5Path.size() - 3) != ".h5" &&
         hdf5Path.substr(hdf5Path.size() - 5) != ".hdf5"))
    {
        hdf5Path += ".h5";
    }

    // Open HDF5 file
    auto &hdf5 = backend_.hdf5();
    if (!hdf5.openFile(hdf5Path))
    {
        QMessageBox::critical(this, tr("Error"),
                              tr("Failed to open HDF5 file:\n%1").arg(filePath));
        return;
    }

    // Initialize datasets for incremental writing
    if (!hdf5.initializeDatasets())
    {
        QMessageBox::warning(this, tr("Warning"),
                             tr("Failed to initialize HDF5 datasets"));
    }

    // Start experiment (clear frame buffers)
    auto &processing = backend_.processing();
    processing.startExperiment();

    // Record experiment start time
    experimentStartTimeNs_ = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();

    experimentActive_ = true;
    statusLabel_->setText("Experiment started");
}

void MainWindow::onStopExperiment()
{
    if (!experimentActive_)
    {
        QMessageBox::information(this, tr("Experiment"),
                                 tr("No experiment is currently running"));
        return;
    }

    // End experiment and flush any remaining frames
    auto &processing = backend_.processing();

    // Wait for any ongoing flush to complete
    if (flushInProgress_ && flushWatcher_)
    {
        flushWatcher_->waitForFinished();
    }

    // Flush any remaining buffered frames (synchronous for final flush)
    auto &hdf5 = backend_.hdf5();
    if (hdf5.isFileOpen())
    {
        size_t flushed = processing.flushBufferedFrames(hdf5);
        if (flushed > 0)
        {
            SPDLOG_INFO("Final flush: {} frames written to HDF5", flushed);
        }
    }

    processing.endExperiment();

    // Get final frame counts (should be empty after flush, but check anyway)
    auto validFrames = processing.getValidFrames();
    auto invalidFrames = processing.getInvalidFrames();

    // Record experiment end time
    uint64_t experimentEndTimeNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count();

    // Save any remaining frames and write experiment info
    if (hdf5.isFileOpen())
    {
        if (!validFrames.empty() || !invalidFrames.empty())
        {
            // Save any remaining frames that weren't flushed
            if (!hdf5.appendFrames(validFrames, invalidFrames))
            {
                QMessageBox::warning(this, tr("Warning"),
                                     tr("Failed to save remaining frames to HDF5"));
            }
        }

        // Write experiment metadata
        size_t totalValid = validFrames.size();
        size_t totalInvalid = invalidFrames.size();
        // Note: We can't easily track total frames written via append, so we use current counts
        // In a production system, you'd want to track cumulative counts
        hdf5.writeExperimentInfo(experimentStartTimeNs_, experimentEndTimeNs,
                                 totalValid, totalInvalid);

        statusLabel_->setText(QString("Experiment saved: %1 valid, %2 invalid frames")
                                  .arg(totalValid)
                                  .arg(totalInvalid));
        hdf5.closeFile();
    }
    else
    {
        statusLabel_->setText("Experiment stopped (HDF5 file not open)");
    }

    experimentActive_ = false;
}

void MainWindow::onUpdateStats()
{
    const auto &cap = backend_.capture();
    const auto &s = cap.stats();
    auto &proc = backend_.processing();

    QString status;
    if (cap.isRunning())
    {
        status = "Camera: running";
        status += QString(" | frames=%1, fps=%2, MB/s=%3")
                      .arg(QString::number(s.framesProcessed.load()))
                      .arg(QString::number(s.lastFrameRate.load()))
                      .arg(QString::number(s.lastDataRateMBps.load()));
    }
    else
    {
        status = "Camera: stopped";
    }

    if (experimentActive_)
    {
        auto validFrames = proc.getValidFrames();
        auto invalidFrames = proc.getInvalidFrames();
        size_t totalBuffered = validFrames.size() + invalidFrames.size();

        // Check if we need to flush (round-robin buffer)
        // Only start a new flush if one isn't already in progress
        size_t flushNeeded = proc.getFlushInterval();
        if (flushNeeded > 0 && totalBuffered >= flushNeeded && !flushInProgress_)
        {
            // Flush frames to disk asynchronously to avoid blocking UI
            flushInProgress_ = true;
            // Capture backend_ by reference - it's a member variable so safe
            QFuture<size_t> future = QtConcurrent::run([this]()
                                                       {
#ifdef _WIN32
                // Lower OS thread priority and optionally set affinity to a non-critical core
                // Background mode (Vista+); falls back to BELOW_NORMAL if unavailable
                if (!SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN)) {
                    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
                }
                const unsigned int cores = std::thread::hardware_concurrency();
                if (cores > 1) {
                    // Prefer the last core
                    const DWORD_PTR mask = (cores >= (sizeof(DWORD_PTR) * 8))
                        ? (static_cast<DWORD_PTR>(1) << ((sizeof(DWORD_PTR) * 8) - 1))
                        : (static_cast<DWORD_PTR>(1) << (cores - 1));
                    SetThreadAffinityMask(GetCurrentThread(), mask);
                }
#endif
                auto& hdf5 = backend_.hdf5();
                auto& proc = backend_.processing();
                return proc.flushBufferedFrames(hdf5); });
            flushWatcher_->setFuture(future);
        }

        status += QString(" | Experiment: active | buffered: valid=%1, invalid=%2")
                      .arg(validFrames.size())
                      .arg(invalidFrames.size());
        if (flushInProgress_)
        {
            status += " (flushing...)";
        }
    }
    else
    {
        status += " | Experiment: inactive";
    }

    statusLabel_->setText(status);
}

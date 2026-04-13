#include "frontend/core/MainWindow.h"
#include "ui_MainWindow.h"

#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QStatusBar>
#include <QTimer>
#include <QTabWidget>
#include <QSplitter>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QSizePolicy>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>

#include "backend/AppBackend.h"
#include "backend/BackgroundCaptureNotifier.h"
#include "backend/services/CaptureService.h"
#include "backend/services/ProcessingService.h"
#include "backend/services/Hdf5Service.h"
#include "backend/services/PlaybackService.h"
#include "backend/services/AutofocusService.h"
#include "frontend/system/PlaybackPanel.h"
#include "frontend/tabs/ConnectTab.h"
#include "frontend/tabs/PreviewPage.h"
#include "frontend/tabs/HdfReviewTab.h"
#include "frontend/tabs/ExperimentMonitoringTab.h"
#include "frontend/dialogs/MonitoringSettingsDialog.h"
#include "frontend/tabs/OverviewTab.h"
#include "frontend/tabs/ConfigTabs.h"
#include "frontend/system/AutoUpdater.h"
#include "frontend/system/DeviceInitManager.h"
#include "frontend/utils/SidebarWidget.h"
#include "frontend/utils/StatisticsPanel.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <spdlog/spdlog.h>
#include <opencv2/core.hpp>
#include <chrono>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QSpinBox>
#include <QLayout>
#include <QThread>
#include <thread>
#include <QMenuBar>
#include <QMenu>
#include "frontend/dialogs/ProcessingSettingsDialog.h"
#include "frontend/dialogs/ConversionFactorDialog.h"
#include "backend/Tools.h"
#include <QCloseEvent>
#ifdef _WIN32
#include <windows.h>
#endif

MainWindow::MainWindow(backend::AppBackend &backend, QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), backend_(backend)
{
    ui->setupUi(this);

    // Connect menu actions
    connect(ui->exitAct, &QAction::triggered, this, &QWidget::close);
    connect(ui->processingSettingsAct, &QAction::triggered, this, [this]()
            {
        SPDLOG_INFO("Opening Processing Settings dialog");
        // Find PlaybackPanel from PreviewPage tab
        PlaybackPanel* playbackPanel = nullptr;
        if (experimentTabs_) {
            // Preview tab is at index 0 within Experiment tab
            if (experimentTabs_->count() > 0) {
                auto* previewPage = qobject_cast<frontend::PreviewPage*>(experimentTabs_->widget(0));
                if (previewPage) {
                    playbackPanel = previewPage->getPlaybackPanel();
                }
            }
        }
        ProcessingSettingsDialog dlg(backend_, playbackPanel, this);
        dlg.exec(); });
    connect(ui->monitoringSettingsAct, &QAction::triggered, this, [this]()
            {
        SPDLOG_INFO("Opening Monitoring Settings dialog");
        // Find ExperimentMonitoringTab
        frontend::ExperimentMonitoringTab* monitoringTab = nullptr;
        if (experimentTabs_) {
            // Monitoring tab is at index 1 within Experiment tab (0=Preview, 1=Monitoring)
            if (experimentTabs_->count() > 1) {
                monitoringTab = qobject_cast<frontend::ExperimentMonitoringTab*>(experimentTabs_->widget(1));
            }
        }
        if (monitoringTab) {
            MonitoringSettingsDialog dlg(monitoringTab, this);
            dlg.exec();
        } });
    connect(ui->conversionFactorAct, &QAction::triggered, this, [this]()
            {
        SPDLOG_INFO("Opening Pixel to Micron Conversion dialog");
        ConversionFactorDialog dlg(backend_, this);
        dlg.exec(); });
    connect(ui->aboutAct, &QAction::triggered, this, [this]()
            {
        const QString v = QCoreApplication::applicationVersion();
        QMessageBox::about(this,
                           tr("About MIB Studio Qt"),
                           tr("MIB Studio Qt\nVersion: %1\n\nProvides camera capture, processing, and HDF5 logging.")
                               .arg(v.isEmpty() ? tr("(unknown)") : v));
    });

    updater_ = new frontend::AutoUpdater(this, this);
    connect(ui->checkUpdatesAct, &QAction::triggered, this, [this]() {
        if (updater_) updater_->checkForUpdates(true);
    });

    // Camera buttons will be added to main tab bar corner widget, not toolbar
    auto *startCaptureAct = new QAction("Start Camera", this);
    auto *stopCaptureAct = new QAction("Stop Camera", this);
    
    // Experiment buttons and indicator will be added to Experiment tab, not toolbar
    startExperimentAct_ = new QAction("Start Experiment", this);
    stopExperimentAct_ = new QAction("Stop Experiment", this);

    // Initialize defaults for this session
    backend_.processing().setInvalidFrameSamplingRate(200);
    backend_.processing().setFlushInterval(200);

    connect(startCaptureAct, &QAction::triggered, this, &MainWindow::onStartCapture);
    connect(stopCaptureAct, &QAction::triggered, this, &MainWindow::onStopCapture);
    connect(startExperimentAct_, &QAction::triggered, this, &MainWindow::onStartExperiment);
    connect(stopExperimentAct_, &QAction::triggered, this, &MainWindow::onStopExperiment);

    statusLabel_ = new QLabel("Idle");
    ui->statusbar->addPermanentWidget(statusLabel_);

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

    // Setup sidebar and main layout
    setupSidebar();

    // Tabs: Connect + Overview + Experiment (Preview + Monitoring) + Review
    connectTab_ = new frontend::ConnectTab(backend_, ui->tabs);
    overviewTab_ = new frontend::OverviewTab(backend_, ui->tabs);
    
    // Create Experiment tab with nested Preview and Monitoring tabs
    experimentTabs_ = new QTabWidget(this);
    auto *previewPage = new frontend::PreviewPage(backend_, experimentTabs_);
    auto *monitoringTab = new frontend::ExperimentMonitoringTab(backend_, experimentTabs_);
    experimentTabs_->addTab(previewPage, tr("Preview"));
    experimentTabs_->addTab(monitoringTab, tr("Monitoring"));
    
    // Connect PlaybackPanel background signal to SidebarWidget
    if (sidebarWidget_ && previewPage) {
        PlaybackPanel* playbackPanel = previewPage->getPlaybackPanel();
        if (playbackPanel) {
            connect(playbackPanel, &PlaybackPanel::backgroundImageSet,
                    sidebarWidget_, &frontend::SidebarWidget::updateBackgroundPreview);
            // Set initial background if one exists
            QImage currentBg = playbackPanel->getBackgroundImage();
            if (!currentBg.isNull()) {
                sidebarWidget_->updateBackgroundPreview(currentBg);
            }
            
            // Connect auto-capture signal
            connect(backend_.backgroundCaptureNotifier(), &backend::BackgroundCaptureNotifier::backgroundAutoCaptured,
                    playbackPanel, &PlaybackPanel::onBackgroundAutoCaptured);
        }
    }
    
    setupCornerWidgets();
    
    auto *hdfReviewTab = new frontend::HdfReviewTab(backend_, ui->tabs);
    ui->tabs->addTab(connectTab_, tr("Connect"));
    ui->tabs->addTab(overviewTab_, tr("Overview"));
    ui->tabs->addTab(experimentTabs_, tr("Experiment"));
    ui->tabs->addTab(hdfReviewTab, tr("Review"));

    connect(connectTab_, &frontend::ConnectTab::connected, this, [this]()
            {
        // On connection, switch to Overview and enable ROI overlay by default.
        if (overviewTab_) {
            overviewTab_->setRoiOverlayVisible(true);
        }
        if (ui->tabs) {
            ui->tabs->setCurrentIndex(1); // Overview tab
        } });

    connect(overviewTab_, &frontend::OverviewTab::roiChanged,
            monitoringTab, &frontend::ExperimentMonitoringTab::updateRoiDisplay);
    connect(overviewTab_, &frontend::OverviewTab::roiChanged,
            this, [this](int offsetX, int offsetY, int width, int height) {
        if (roiLabel_)
            roiLabel_->setText(tr("ROI: %1 x %2 @ (%3, %4)").arg(width).arg(height).arg(offsetX).arg(offsetY));
    });
    // Initialize both displays with current ROI values
    {
        int ox = static_cast<int>(overviewTab_->roiPosition().x());
        int oy = static_cast<int>(overviewTab_->roiPosition().y());
        int w = overviewTab_->roiWidth();
        int h = overviewTab_->roiHeight();
        monitoringTab->updateRoiDisplay(ox, oy, w, h);
        if (roiLabel_)
            roiLabel_->setText(tr("ROI: %1 x %2 @ (%3, %4)").arg(w).arg(h).arg(ox).arg(oy));
    }

    connect(connectTab_, &frontend::ConnectTab::noCamerasFound, this, &MainWindow::onNoCamerasFound);

    // Device init manager runs camera and nanopositioner auto-connect off the UI thread
    initManager_ = new frontend::DeviceInitManager(backend_, this);
    initManager_->setConnectTab(connectTab_);
    initManager_->setNanopositionerTab(sidebarWidget_ ? sidebarWidget_->nanopositionerTab() : nullptr);
    connectTab_->setDeviceInitManager(initManager_);

    // Connect tab change signal for auto-applying camera scripts
    connect(ui->tabs, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);

    // Initialize button states (start enabled, stop disabled)
    updateExperimentButtonStates();
    
    // Initialize tab states (all tabs enabled initially since no experiment is active)
    updateTabStates();

    // Auto-connect on startup (camera at 400 ms, then nanopositioner after camera completes)
    initManager_->start();

    // Quiet update check on startup (only prompts if an update is available)
    QTimer::singleShot(1500, this, [this]() {
        if (updater_) updater_->checkForUpdates(false);
    });
}

MainWindow::~MainWindow() {
    delete ui;
}

void MainWindow::setupSidebar()
{
    // Remove the existing tabs widget from the central widget layout
    ui->verticalLayout->removeWidget(ui->tabs);

    // Create horizontal splitter
    mainSplitter_ = new QSplitter(Qt::Horizontal, ui->centralwidget);
    mainSplitter_->setChildrenCollapsible(false);

    // Create sidebar widget
    sidebarWidget_ = new frontend::SidebarWidget(backend_, mainSplitter_);
    connect(sidebarWidget_, &frontend::SidebarWidget::collapseStateChanged, this, [this](bool collapsed) {
        if (mainSplitter_ && mainSplitter_->count() >= 2) {
            int targetWidth = collapsed ? 30 : sidebarWidget_->expandedWidth();
            QList<int> sizes = mainSplitter_->sizes();
            if (sizes.size() >= 2) {
                sizes[0] = targetWidth;
                mainSplitter_->setSizes(sizes);
            }
        }
    });

    // Add sidebar to splitter
    mainSplitter_->addWidget(sidebarWidget_);
    
    // Add tabs widget to splitter
    mainSplitter_->addWidget(ui->tabs);

    // Set splitter stretch factors (sidebar: 0, tabs: 1)
    mainSplitter_->setStretchFactor(0, 0);
    mainSplitter_->setStretchFactor(1, 1);

    // Set initial sizes - sidebar width depends on collapsed state
    int sidebarWidth = sidebarWidget_->isCollapsed() ? 30 : sidebarWidget_->expandedWidth();
    mainSplitter_->setSizes({sidebarWidth, 1000});

    // Add splitter to central widget layout
    ui->verticalLayout->addWidget(mainSplitter_);
}

void MainWindow::setupCornerWidgets() {
    // Create corner widget for experiment controls (on same row as tabs)
    auto *experimentControlsWidget = new QWidget(this);
    auto *experimentControlsLayout = new QHBoxLayout(experimentControlsWidget);
    experimentControlsLayout->setContentsMargins(5, 0, 5, 0);
    experimentControlsLayout->setSpacing(5);
    
    // Create experiment indicator widget (colored rectangle)
    experimentIndicator_ = new QLabel(experimentControlsWidget);
    experimentIndicator_->setFixedSize(20, 20);
    experimentIndicator_->setStyleSheet("background-color: gray; border: 1px solid black;");
    experimentIndicator_->setToolTip(tr("Experiment status indicator"));
    experimentControlsLayout->addWidget(experimentIndicator_);
    
    // ROI display label
    roiLabel_ = new QLabel(tr("ROI: --"), experimentControlsWidget);
    roiLabel_->setStyleSheet("font-weight: bold; padding: 0 8px;");
    experimentControlsLayout->addWidget(roiLabel_);

    // Create push buttons and connect them to actions
    startExperimentBtn_ = new QPushButton(startExperimentAct_->text(), experimentControlsWidget);
    stopExperimentBtn_ = new QPushButton(stopExperimentAct_->text(), experimentControlsWidget);
    connect(startExperimentBtn_, &QPushButton::clicked, startExperimentAct_, &QAction::trigger);
    connect(stopExperimentBtn_, &QPushButton::clicked, stopExperimentAct_, &QAction::trigger);
    experimentControlsLayout->addWidget(startExperimentBtn_);
    experimentControlsLayout->addWidget(stopExperimentBtn_);
    
    // Add controls widget to the corner of the tab bar (same row as tabs)
    experimentTabs_->setCornerWidget(experimentControlsWidget, Qt::TopRightCorner);
    
    // Create corner widget for camera controls (on same row as main tabs)
    auto *cameraControlsWidget = new QWidget(this);
    auto *cameraControlsLayout = new QHBoxLayout(cameraControlsWidget);
    cameraControlsLayout->setContentsMargins(5, 0, 5, 0);
    cameraControlsLayout->setSpacing(5);
    
    // Create push buttons and connect them to actions
    startCameraBtn_ = new QPushButton("Start Camera", cameraControlsWidget);
    stopCameraBtn_ = new QPushButton("Stop Camera", cameraControlsWidget);
    connect(startCameraBtn_, &QPushButton::clicked, this, &MainWindow::onStartCapture);
    connect(stopCameraBtn_, &QPushButton::clicked, this, &MainWindow::onStopCapture);
    cameraControlsLayout->addWidget(startCameraBtn_);
    cameraControlsLayout->addWidget(stopCameraBtn_);
    
    // Add controls widget to the corner of the main tab bar (same row as tabs)
    ui->tabs->setCornerWidget(cameraControlsWidget, Qt::TopRightCorner);
}

void MainWindow::updateExperimentButtonStates()
{
    if (startExperimentAct_ && stopExperimentAct_)
    {
        // Disable start when experiment is active, enable when inactive
        startExperimentAct_->setEnabled(!experimentActive_);
        // Disable stop when experiment is inactive, enable when active
        stopExperimentAct_->setEnabled(experimentActive_);
    }
    
    // Update button enabled states to match actions
    if (startExperimentBtn_)
    {
        startExperimentBtn_->setEnabled(startExperimentAct_ ? startExperimentAct_->isEnabled() : false);
    }
    if (stopExperimentBtn_)
    {
        stopExperimentBtn_->setEnabled(stopExperimentAct_ ? stopExperimentAct_->isEnabled() : false);
    }

    // Update visual indicator
    if (experimentIndicator_)
    {
        if (experimentActive_)
        {
            // Green/active color when experiment is running
            experimentIndicator_->setStyleSheet("background-color: #00ff00; border: 1px solid black;");
            experimentIndicator_->setToolTip(tr("Experiment is running"));
        }
        else
        {
            // Gray/inactive color when no experiment
            experimentIndicator_->setStyleSheet("background-color: gray; border: 1px solid black;");
            experimentIndicator_->setToolTip(tr("No experiment running"));
        }
    }

    // Update tab states
    updateTabStates();
}

void MainWindow::updateTabStates()
{
    if (!ui->tabs)
        return;

    // Disable Overview tab (index 1) and Review tab (index 3) during experiment
    ui->tabs->setTabEnabled(1, !experimentActive_); // Overview
    ui->tabs->setTabEnabled(3, !experimentActive_); // Review
}

void MainWindow::onStartCapture()
{
    auto &cap = backend_.capture();
    if (cap.isRunning())
    {
        QMessageBox::information(this, tr("Start Camera"),
                                 tr("Camera is already running."));
        return;
    }

    // Guard: Cannot start camera unless a camera has been connected (hardware or mock)
    if (!backend_.isCameraConfigured())
    {
        QMessageBox::warning(this, tr("Start Camera"),
                             tr("No camera is configured. Please connect to a camera or configure a mock camera first."));
        statusLabel_->setText("Camera not configured");
        return;
    }

    // Start capture only (no experiment)
    if (cap.start())
    {
        statsTimer_->start();
        statusLabel_->setText("Camera running");
    }
    else
    {
        QMessageBox::warning(this, tr("Start Camera"),
                             tr("Failed to start camera. Please check camera connection and try again."));
        statusLabel_->setText("Camera start failed");
    }
}

void MainWindow::onStopCapture()
{
    auto &cap = backend_.capture();
    if (!cap.isRunning())
    {
        QMessageBox::information(this, tr("Stop Camera"),
                                 tr("Camera is not currently running."));
        return;
    }

    // Guard: During experiment cannot stop camera before stopping experiment
    if (experimentActive_)
    {
        QMessageBox::warning(this, tr("Stop Camera"),
                             tr("Cannot stop camera while experiment is active. Please stop the experiment first."));
        statusLabel_->setText("Cannot stop camera during experiment");
        return;
    }

    // Stop capture only (don't end experiment)
    cap.stop();
    statsTimer_->stop();
    backend_.processing().resetRealtimeMetrics();
    statusLabel_->setText("Camera stopped");
}

void MainWindow::onStartExperiment()
{
    if (experimentActive_)
    {
        QMessageBox::information(this, tr("Experiment"),
                                 tr("Experiment is already running"));
        return;
    }

    // Guard: Experiment cannot start without first starting camera
    if (!backend_.capture().isRunning())
    {
        QMessageBox::warning(this, tr("Start Experiment"),
                             tr("Camera must be running before starting an experiment. Please start the camera first."));
        statusLabel_->setText("Camera not running");
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
    updateExperimentButtonStates(); // This will also call updateTabStates() to disable Overview and Review tabs
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
    backend_.processing().resetRealtimeMetrics();

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

        // Write experiment metadata (including background image for reproducibility if set)
        size_t totalValid = validFrames.size();
        size_t totalInvalid = invalidFrames.size();
        // Note: We can't easily track total frames written via append, so we use current counts
        // In a production system, you'd want to track cumulative counts
        auto processingConfig = processing.getProcessingConfig();
        auto roi = processing.getRealtimeRoi();
        cv::Mat bg = processing.getRealtimeBackgroundGray();
        hdf5.writeExperimentInfo(experimentStartTimeNs_, experimentEndTimeNs,
                                 totalValid, totalInvalid, processingConfig, roi,
                                 bg.empty() ? nullptr : &bg);

        // Save full config.json content for backtracking
        std::string configJson = backend_.getLastConfigJson();
        if (!configJson.empty()) {
            hdf5.writeConfigJson(configJson);
        }

        // Note: Chart snapshots are no longer saved during experiment stop.
        // Charts are now generated on-demand from HDF5 data in the Review tab.

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
    updateExperimentButtonStates(); // This will also call updateTabStates() to enable Overview and Review tabs
}

void MainWindow::onUpdateStats()
{
    const auto &cap = backend_.capture();
    const auto &s = cap.stats();
    auto &proc = backend_.processing();
    const uint64_t tFetchStartUs = backend::Tools::getTimestamp();
    auto validFrames = proc.getValidFrames();
    auto invalidFrames = proc.getInvalidFrames();
    const uint64_t tFetchEndUs = backend::Tools::getTimestamp();
    const double fetchMs = static_cast<double>(tFetchEndUs - tFetchStartUs) / 1000.0;

    // Collect statistics data
    double displayFps = 0.0;
    if (experimentTabs_ && experimentTabs_->count() > 0) {
        auto* previewPage = qobject_cast<frontend::PreviewPage*>(experimentTabs_->widget(0));
        if (previewPage) {
            auto* playbackPanel = previewPage->getPlaybackPanel();
            if (playbackPanel) {
                displayFps = playbackPanel->getDisplayFps();
            }
        }
    }
    const double algoAvgUs = proc.getAlgoAvgUs1s();
    const double validFps = proc.getValidFps1s();
    const double invalidFps = proc.getInvalidFps1s();
    const uint64_t totalValidFlushed = proc.getTotalValidFlushed();

    const uint64_t nowUs = backend::Tools::getTimestamp();
    const double algoAvgUsAgeMs = (nowUs - proc.getAlgoAvgUs1sUpdatedUs()) / 1000.0;
    const double meanRingRatioAgeMs = (nowUs - backend_.autofocus().getLastRingRatioUpdateUs()) / 1000.0;

    // Calculate experiment runtime
    double experimentRuntimeSeconds = 0.0;
    if (experimentActive_ && experimentStartTimeNs_ > 0) {
        uint64_t currentTimeNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count();
        experimentRuntimeSeconds = static_cast<double>(currentTimeNs - experimentStartTimeNs_) / 1e9;
    }

    // Update statistics panel if sidebar exists
    if (sidebarWidget_ && sidebarWidget_->statisticsPanel()) {
        frontend::StatisticsData data;
        data.displayFps = displayFps;
        data.algoAvgUs = algoAvgUs;
        data.validFps = validFps;
        data.invalidFps = invalidFps;
        data.totalValidFlushed = totalValidFlushed;
        data.cameraRunning = cap.isRunning();
        data.cameraFps = s.lastFrameRate.load();
        data.cameraDataRateMBps = s.lastDataRateMBps.load();
        data.meanRingRatio = backend_.autofocus().getMedianRingRatio();
        data.experimentActive = experimentActive_;
        data.validBuffered = validFrames.size();
        data.invalidBuffered = invalidFrames.size();
        data.flushInProgress = flushInProgress_;
        data.experimentRuntimeSeconds = experimentRuntimeSeconds;
        data.algoAvgUsAgeMs = algoAvgUsAgeMs;
        data.meanRingRatioAgeMs = meanRingRatioAgeMs;

        sidebarWidget_->statisticsPanel()->updateStatistics(data);
    }

    // Also update status bar for backward compatibility
    QString status;
    status = QString("Display=%1 fps | Algo=%2 us | Valid=%3/s | Invalid=%4/s | Flushed(valid)=%5")
                 .arg(QString::number(displayFps, 'f', 1))
                 .arg(QString::number(algoAvgUs, 'f', 1))
                 .arg(QString::number(validFps, 'f', 1))
                 .arg(QString::number(invalidFps, 'f', 1))
                 .arg(QString::number(static_cast<qulonglong>(totalValidFlushed)));

    // Camera transport stats
    if (cap.isRunning()) {
        status += QString(" | Camera=%1 fps, %2 MB/s")
                      .arg(QString::number(s.lastFrameRate.load()))
                      .arg(QString::number(s.lastDataRateMBps.load()));
    } else {
        status += " | Camera: stopped";
    }

    // Append live ring width (median from AutofocusService, same value used by autofocus)
    {
        const double ringWidth = backend_.autofocus().getMedianRingRatio();
        status += QString(" | Ring width=%1").arg(QString::number(ringWidth, 'f', 3));
    }

    if (experimentActive_)
    {
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

        // Throttled diagnostic log (~1 Hz)
        static uint64_t lastDiagLogUs = 0;
        const uint64_t nowUs = backend::Tools::getTimestamp();
        if (nowUs - lastDiagLogUs >= 1'000'000ULL) {
            uint64_t earliest = 0, latest = 0;
            size_t count = 0;
            backend_.playback().queryRange(earliest, latest, count);
            const double memMB = backend::Tools::getProcessMemoryMB();
            SPDLOG_INFO("MainWindow stats: buffer_fetch_ms={:.3f}, valid={}, invalid={}, total={}, playback_range=[{},{}] count={}, flush_interval={}, flushing={}, mem_mb={:.1f}",
                        fetchMs, validFrames.size(), invalidFrames.size(), totalBuffered, earliest, latest, count, flushNeeded, flushInProgress_ ? 1 : 0, memMB);
            lastDiagLogUs = nowUs;
        }
    }
    else
    {
        status += " | Experiment: inactive";
    }

    statusLabel_->setText(status);
}

void MainWindow::startExperimentServices()
{
    if (experimentServicesActive_) {
        return; // Already active
    }

    auto frameStore = backend_.getFrameStore();
    if (!frameStore) {
        SPDLOG_ERROR("MainWindow::startExperimentServices: FrameStore is null");
        return;
    }

    backend_.processing().startRealtime(frameStore);
    backend_.playback().play();
    experimentServicesActive_ = true;
    SPDLOG_INFO("MainWindow: Experiment services started");
}

void MainWindow::stopExperimentServices()
{
    if (!experimentServicesActive_) {
        return; // Already stopped
    }

    backend_.processing().stopRealtime();
    backend_.playback().pause();
    experimentServicesActive_ = false;
    SPDLOG_INFO("MainWindow: Experiment services stopped");
}

void MainWindow::onNoCamerasFound()
{
    if (ui->tabs)
    {
        ui->tabs->setCurrentIndex(0); // Connect tab
    }

    QMessageBox box(this);
    box.setWindowTitle(tr("No camera found"));
    box.setIcon(QMessageBox::Information);
    box.setText(tr("No camera was found.\n\n"
                   "Please check that the camera is powered on and that the camera LED is green (ready).\n"
                   "Then try connecting again."));

    auto *tryAgainBtn = box.addButton(tr("Try again"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Ok);

    box.exec();

    if (box.clickedButton() == tryAgainBtn)
    {
        // Defer to avoid re-entrancy if discovery immediately emits noCamerasFound().
        QTimer::singleShot(0, this, [this]() {
            if (connectTab_) connectTab_->tryAutoConnect();
        });
    }
}

void MainWindow::onTabChanged(int index)
{
    // Guard: Once experiment started, overview is disabled until end of experiment
    if (index == 1 && experimentActive_) {
        QMessageBox::warning(this, tr("Overview Tab"),
                             tr("Overview tab is disabled while an experiment is active. Please stop the experiment first."));
        // Switch back to previous tab (or Experiment tab)
        if (ui->tabs) {
            ui->tabs->setCurrentIndex(2); // Switch to Experiment tab
        }
        return;
    }

    // Guard: Cannot review when there is an experiment going on
    if (index == 3 && experimentActive_) {
        QMessageBox::warning(this, tr("Review Tab"),
                             tr("Review tab is disabled while an experiment is active. Please stop the experiment first."));
        // Switch back to previous tab (or Experiment tab)
        if (ui->tabs) {
            ui->tabs->setCurrentIndex(2); // Switch to Experiment tab
        }
        return;
    }

    // Handle service lifecycle for Overview (index 1) + Experiment (index 2)
    // Both tabs rely on playback/processing to show live frames.
    const int OVERVIEW_TAB_INDEX = 1;
    const int EXPERIMENT_TAB_INDEX = 2;
    
    if (index == OVERVIEW_TAB_INDEX || index == EXPERIMENT_TAB_INDEX) {
        // Switching TO Overview/Experiment: start services
        startExperimentServices();
        // Only run processing pipeline when Experiment tab is visible. When Overview is visible,
        // disable processing to avoid wrong processing times, slowdown, and overview frames
        // being used as auto-background.
        backend_.processing().setRealtimeEnabled(index == EXPERIMENT_TAB_INDEX);
    } else if (experimentServicesActive_) {
        // Switching away from Overview/Experiment: stop services
        stopExperimentServices();
    }

    // Auto-start camera when navigating to Overview
    if (index == OVERVIEW_TAB_INDEX)
    {
        auto &cap = backend_.capture();
        if (!cap.isRunning() && backend_.isCameraConfigured())
        {
            SPDLOG_INFO("MainWindow: auto-starting camera on Overview navigation");
            if (cap.start())
            {
                if (statsTimer_)
                    statsTimer_->start();
                if (statusLabel_)
                    statusLabel_->setText("Camera running");
            }
            else
            {
                QMessageBox::warning(this, tr("Start Camera"),
                                     tr("Failed to start camera. Please check camera connection and try again."));
                if (statusLabel_)
                    statusLabel_->setText("Camera start failed");
            }
        }
    }

    // Guard: Camera script application - skip during experiment
    // Only handle script application for switches between Overview (index 1) and Experiment (index 2)
    if (index != 1 && index != 2) {
        return;
    }

    // Skip script application if experiment is active
    if (experimentActive_) {
        SPDLOG_WARN("MainWindow::onTabChanged: Skipping script application during active experiment");
        return;
    }

    // Check if camera is currently running
    bool wasRunning = backend_.capture().isRunning();

    QString scriptPath;
    
    if (index == 1) {
        // Overview tab
        if (!overviewTab_) {
            SPDLOG_WARN("MainWindow::onTabChanged: overviewTab_ is null");
            return;
        }
        scriptPath = overviewTab_->currentJsPath();
    } else if (index == 2) {
        // Experiment tab
        if (!experimentTabs_) {
            SPDLOG_WARN("MainWindow::onTabChanged: experimentTabs_ is null");
            return;
        }
        // Get PreviewPage from Experiment tab (index 0)
        auto* previewPage = qobject_cast<frontend::PreviewPage*>(experimentTabs_->widget(0));
        if (!previewPage) {
            SPDLOG_WARN("MainWindow::onTabChanged: PreviewPage is null");
            return;
        }
        auto* configTabs = previewPage->getConfigTabs();
        if (!configTabs) {
            SPDLOG_WARN("MainWindow::onTabChanged: ConfigTabs is null");
            return;
        }
        scriptPath = configTabs->currentJsPath();
    }

    if (scriptPath.isEmpty()) {
        SPDLOG_WARN("MainWindow::onTabChanged: Script path is empty");
        return;
    }

    std::string backendErr;
    if (backend_.isMindVisionCameraSelected()) {
        // For MindVision: copy default JSON from resource if the file doesn't exist yet
        if (!QFileInfo::exists(scriptPath)) {
            const QString resourcePath = (index == 1)
                ? QStringLiteral(":/defaults/mindvisionOverviewConfig.json")
                : QStringLiteral(":/defaults/mindvisionConfig.json");
            QDir().mkpath(QFileInfo(scriptPath).absolutePath());
            QFile::copy(resourcePath, scriptPath);
        }
        if (!QFileInfo::exists(scriptPath)) {
            SPDLOG_WARN("MainWindow::onTabChanged: MindVision config file could not be created: {}",
                        scriptPath.toStdString());
            return;
        }
        SPDLOG_INFO("MainWindow::onTabChanged: Auto-applying MindVision config from {}", scriptPath.toStdString());
        if (!backend_.applyMindVisionConfigFromFile(scriptPath.toStdString(), &backendErr)) {
            SPDLOG_ERROR("MainWindow::onTabChanged: Failed to apply MindVision config: {}", backendErr);
            return;
        }
        // Propagate ROI from MindVision JSON config to experiment monitoring tab
        {
            QFile f(scriptPath);
            if (f.open(QIODevice::ReadOnly)) {
                QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
                f.close();
                int ox = obj.value("offset_x").toInt(0);
                int oy = obj.value("offset_y").toInt(0);
                int w  = obj.value("width").toInt(0);
                int h  = obj.value("height").toInt(0);
                if (auto* mt = qobject_cast<frontend::ExperimentMonitoringTab*>(
                        experimentTabs_ ? experimentTabs_->widget(1) : nullptr)) {
                    mt->updateRoiDisplay(ox, oy, w, h);
                }
                if (roiLabel_)
                    roiLabel_->setText(tr("ROI: %1 x %2 @ (%3, %4)").arg(w).arg(h).arg(ox).arg(oy));
            }
        }
    } else {
        // eGrabber path: script file must exist
        if (!QFileInfo::exists(scriptPath)) {
            SPDLOG_WARN("MainWindow::onTabChanged: Script file does not exist: {}", scriptPath.toStdString());
            return;
        }
        SPDLOG_INFO("MainWindow::onTabChanged: Auto-applying camera script from {}", scriptPath.toStdString());
        if (!backend_.applyCameraScriptFromFile(scriptPath.toStdString(), &backendErr)) {
            SPDLOG_ERROR("MainWindow::onTabChanged: Failed to apply script: {}", backendErr);
            return;
        }
    }

    // If camera was running, restart it
    if (wasRunning) {
        SPDLOG_INFO("MainWindow::onTabChanged: Restarting camera after script application");
        if (!backend_.capture().start()) {
            SPDLOG_ERROR("MainWindow::onTabChanged: Failed to restart camera after script application");
            statusLabel_->setText("Camera script applied, but restart failed");
        } else {
            SPDLOG_INFO("MainWindow::onTabChanged: Camera restarted successfully");
            statsTimer_->start();
            statusLabel_->setText("Camera running");
        }
    } else {
        SPDLOG_INFO("MainWindow::onTabChanged: Camera script applied (camera was not running)");
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // Guard: Warn if closing during active experiment
    if (experimentActive_) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this,
            tr("Close Application"),
            tr("An experiment is currently active. Closing the application may result in data loss.\n\nDo you want to close anyway?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No
        );
        if (reply != QMessageBox::Yes) {
            event->ignore();
            return;
        }
    }

    // Ensure experiment services are stopped before closing
    if (experimentServicesActive_) {
        stopExperimentServices();
    }
    QMainWindow::closeEvent(event);
}

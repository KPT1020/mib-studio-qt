#include "frontend/dialogs/ProcessingSettingsDialog.h"
#include "ui_ProcessingSettingsDialog.h"

#include <QComboBox>
#include <QPushButton>

#include <spdlog/spdlog.h>

#include "backend/app/AppBackend.h"
#include "backend/processing/ProcessingService.h"
#include "backend/playback/PlaybackService.h"
#include "backend/playback/FrameStore.h"
#include "frontend/system/PlaybackPanel.h"

ProcessingSettingsDialog::ProcessingSettingsDialog(backend::AppBackend& backend, PlaybackPanel* playbackPanel, QWidget* parent)
    : QDialog(parent), ui(new Ui::ProcessingSettingsDialog), backend_(backend), playbackPanel_(playbackPanel) {
    ui->setupUi(this);

    // Load current values from backend
    ui->invalidSamplingSpin->setValue(static_cast<int>(backend_.processing().getInvalidFrameSamplingRate()));
    ui->flushIntervalSpin->setValue(static_cast<int>(backend_.processing().getFlushInterval()));
    ui->dropFramesCheck->setChecked(backend_.processing().getRealtimeDropFrames());
    const auto realtimeMode = backend_.processing().getRealtimeProcessingMode();
    ui->realtimeModeCombo->setCurrentIndex(
        realtimeMode == backend::services::ProcessingService::RealtimeProcessingMode::AsyncBatch ? 1 : 0);
    const auto batchSettings = backend_.processing().getRealtimeBatchSettings();
    ui->batchSizeSpin->setValue(static_cast<int>(batchSettings.batchSize));
    ui->batchQueueSpin->setValue(static_cast<int>(batchSettings.maxQueuedFrames));
    ui->batchWorkersSpin->setValue(static_cast<int>(batchSettings.workerCount));
    ui->batchDelaySpin->setValue(batchSettings.maxBatchDelayMs);
    updateBatchControlsEnabled();

    // Update ROI limits and load current values
    updateRoiLimits();

    connect(ui->realtimeModeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { updateBatchControlsEnabled(); });

    connect(ui->buttons, &QDialogButtonBox::accepted, this, [this]() {
        applySettings();
        accept();
    });
    connect(ui->buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(ui->buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this](bool) { onApply(); });
}

ProcessingSettingsDialog::~ProcessingSettingsDialog() {
    delete ui;
}

void ProcessingSettingsDialog::onApply() {
    applySettings();
}

void ProcessingSettingsDialog::updateBatchControlsEnabled() {
    const bool batchMode = ui->realtimeModeCombo->currentIndex() == 1;
    ui->batchSizeLabel->setEnabled(batchMode);
    ui->batchSizeSpin->setEnabled(batchMode);
    ui->batchQueueLabel->setEnabled(batchMode);
    ui->batchQueueSpin->setEnabled(batchMode);
    ui->batchWorkersLabel->setEnabled(batchMode);
    ui->batchWorkersSpin->setEnabled(batchMode);
    ui->batchDelayLabel->setEnabled(batchMode);
    ui->batchDelaySpin->setEnabled(batchMode);
}

void ProcessingSettingsDialog::updateRoiLimits() {
    int maxWidth = 10000;
    int maxHeight = 10000;

    // Try to get image dimensions from PlaybackPanel first
    if (playbackPanel_) {
        QSize dims = playbackPanel_->getImageDimensions();
        if (dims.width() > 0 && dims.height() > 0) {
            maxWidth = dims.width();
            maxHeight = dims.height();
        }
    }

    // Fallback: try to get from backend latest frame
    if (maxWidth == 10000 || maxHeight == 10000) {
        backend::playback::Frame f;
        if (backend_.playback().fetchLatest(f)) {
            maxWidth = static_cast<int>(f.width);
            maxHeight = static_cast<int>(f.height);
        }
    }

    // Update spinbox maximums
    ui->roiXSpin->setMaximum(std::max(0, maxWidth - 1));
    ui->roiYSpin->setMaximum(std::max(0, maxHeight - 1));
    ui->roiWidthSpin->setMaximum(maxWidth);
    ui->roiHeightSpin->setMaximum(maxHeight);

    // Load current ROI values
    QRect currentRoi;
    if (playbackPanel_) {
        currentRoi = playbackPanel_->getRoi();
    } else {
        // Get from backend
        auto backendRoi = backend_.processing().getRealtimeRoi();
        if (backendRoi.w > 0 && backendRoi.h > 0) {
            currentRoi = QRect(backendRoi.x, backendRoi.y, backendRoi.w, backendRoi.h);
        }
    }

    if (currentRoi.isValid() && !currentRoi.isNull()) {
        ui->roiXSpin->setValue(currentRoi.x());
        ui->roiYSpin->setValue(currentRoi.y());
        ui->roiWidthSpin->setValue(currentRoi.width());
        ui->roiHeightSpin->setValue(currentRoi.height());
    } else {
        // Default to full image or clear
        ui->roiXSpin->setValue(0);
        ui->roiYSpin->setValue(0);
        ui->roiWidthSpin->setValue(maxWidth > 0 ? maxWidth : 1);
        ui->roiHeightSpin->setValue(maxHeight > 0 ? maxHeight : 1);
    }
}

void ProcessingSettingsDialog::applySettings() {
    auto& proc = backend_.processing();
    const int invalidNth = ui->invalidSamplingSpin->value();
    const int flushEvery = ui->flushIntervalSpin->value();

    proc.setInvalidFrameSamplingRate(static_cast<size_t>(invalidNth));
    proc.setFlushInterval(static_cast<size_t>(flushEvery));
    proc.setRealtimeDropFrames(ui->dropFramesCheck->isChecked());

    backend::services::ProcessingService::RealtimeBatchSettings batchSettings;
    batchSettings.batchSize = static_cast<size_t>(ui->batchSizeSpin->value());
    batchSettings.maxQueuedFrames = static_cast<size_t>(ui->batchQueueSpin->value());
    batchSettings.workerCount = static_cast<size_t>(ui->batchWorkersSpin->value());
    batchSettings.maxBatchDelayMs = ui->batchDelaySpin->value();
    proc.setRealtimeBatchSettings(batchSettings);

    const auto realtimeMode = ui->realtimeModeCombo->currentIndex() == 1
                                  ? backend::services::ProcessingService::RealtimeProcessingMode::AsyncBatch
                                  : backend::services::ProcessingService::RealtimeProcessingMode::Inline;
    proc.setRealtimeProcessingMode(realtimeMode);

    SPDLOG_INFO("Processing settings applied: invalidNth={}, flushEvery={}, realtimeMode={}, batchSize={}, batchQueue={}, batchWorkers={}, batchDelayMs={}",
                invalidNth, flushEvery,
                realtimeMode == backend::services::ProcessingService::RealtimeProcessingMode::AsyncBatch ? "async_batch" : "inline",
                batchSettings.batchSize, batchSettings.maxQueuedFrames,
                batchSettings.workerCount, batchSettings.maxBatchDelayMs);

    // Apply ROI settings
    const int roiX = ui->roiXSpin->value();
    const int roiY = ui->roiYSpin->value();
    const int roiW = ui->roiWidthSpin->value();
    const int roiH = ui->roiHeightSpin->value();

    // Get current image dimensions for validation
    int maxWidth = ui->roiWidthSpin->maximum();
    int maxHeight = ui->roiHeightSpin->maximum();

    // Validate ROI bounds
    int clampedX = std::max(0, std::min(roiX, maxWidth - 1));
    int clampedY = std::max(0, std::min(roiY, maxHeight - 1));
    int clampedW = std::max(1, std::min(roiW, maxWidth - clampedX));
    int clampedH = std::max(1, std::min(roiH, maxHeight - clampedY));

    QRect roi(clampedX, clampedY, clampedW, clampedH);

    // Apply to PlaybackPanel if available
    if (playbackPanel_) {
        playbackPanel_->setRoi(roi);
    }

    // Also sync to backend
    backend::services::ProcessingService::Roi backendRoi{};
    backendRoi.x = clampedX;
    backendRoi.y = clampedY;
    backendRoi.w = clampedW;
    backendRoi.h = clampedH;
    proc.setRealtimeRoi(backendRoi);

    SPDLOG_INFO("ROI settings applied: x={}, y={}, w={}, h={}", clampedX, clampedY, clampedW, clampedH);
}



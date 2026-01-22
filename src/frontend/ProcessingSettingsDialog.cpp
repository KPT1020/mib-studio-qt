#include "frontend/ProcessingSettingsDialog.h"
#include "ui_ProcessingSettingsDialog.h"

#include <QPushButton>

#include <spdlog/spdlog.h>

#include "backend/AppBackend.h"
#include "backend/services/ProcessingService.h"
#include "backend/services/PlaybackService.h"
#include "backend/playback/FrameStore.h"
#include "frontend/PlaybackPanel.h"

ProcessingSettingsDialog::ProcessingSettingsDialog(backend::AppBackend& backend, PlaybackPanel* playbackPanel, QWidget* parent)
    : QDialog(parent), ui(new Ui::ProcessingSettingsDialog), backend_(backend), playbackPanel_(playbackPanel) {
    ui->setupUi(this);

    // Load current values from backend
    ui->invalidSamplingSpin->setValue(static_cast<int>(backend_.processing().getInvalidFrameSamplingRate()));
    ui->flushIntervalSpin->setValue(static_cast<int>(backend_.processing().getFlushInterval()));
    ui->dropFramesCheck->setChecked(backend_.processing().getRealtimeDropFrames());

    // Update ROI limits and load current values
    updateRoiLimits();

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
    SPDLOG_INFO("Processing settings applied: invalidNth={}, flushEvery={}", invalidNth, flushEvery);

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



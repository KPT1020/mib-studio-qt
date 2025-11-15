#include "frontend/ProcessingSettingsDialog.h"

#include <QFormLayout>
#include <QSpinBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLabel>

#include <spdlog/spdlog.h>

#include "backend/AppBackend.h"
#include "backend/services/ProcessingService.h"
#include "backend/services/PlaybackService.h"
#include "backend/playback/FrameStore.h"
#include "frontend/PlaybackPanel.h"

ProcessingSettingsDialog::ProcessingSettingsDialog(backend::AppBackend& backend, PlaybackPanel* playbackPanel, QWidget* parent)
    : QDialog(parent), backend_(backend), playbackPanel_(playbackPanel) {
    setWindowTitle(tr("Processing Settings"));
    setModal(true);

    auto* layout = new QFormLayout(this);

    invalidSamplingSpin_ = new QSpinBox(this);
    invalidSamplingSpin_->setMinimum(1);
    invalidSamplingSpin_->setMaximum(10000);
    invalidSamplingSpin_->setSuffix(tr("th frame"));
    invalidSamplingSpin_->setToolTip(tr("Save every Nth invalid frame (1 = save all, higher = fewer frames)"));

    flushIntervalSpin_ = new QSpinBox(this);
    flushIntervalSpin_->setMinimum(1);
    flushIntervalSpin_->setMaximum(10000);
    flushIntervalSpin_->setSuffix(tr(" frames"));
    flushIntervalSpin_->setToolTip(tr("Flush buffered frames to HDF5 every N frames"));

    layout->addRow(tr("Invalid frame sampling"), invalidSamplingSpin_);
    layout->addRow(tr("Flush interval"), flushIntervalSpin_);

    // Load current values from backend
    invalidSamplingSpin_->setValue(static_cast<int>(backend_.processing().getInvalidFrameSamplingRate()));
    flushIntervalSpin_->setValue(static_cast<int>(backend_.processing().getFlushInterval()));

    // ROI controls
    layout->addRow(new QLabel(tr("<b>Region of Interest (ROI)</b>"), this));

    roiXSpin_ = new QSpinBox(this);
    roiXSpin_->setMinimum(0);
    roiXSpin_->setToolTip(tr("ROI X position (left edge)"));

    roiYSpin_ = new QSpinBox(this);
    roiYSpin_->setMinimum(0);
    roiYSpin_->setToolTip(tr("ROI Y position (top edge)"));

    roiWidthSpin_ = new QSpinBox(this);
    roiWidthSpin_->setMinimum(1);
    roiWidthSpin_->setToolTip(tr("ROI width"));

    roiHeightSpin_ = new QSpinBox(this);
    roiHeightSpin_->setMinimum(1);
    roiHeightSpin_->setToolTip(tr("ROI height"));

    layout->addRow(tr("ROI X"), roiXSpin_);
    layout->addRow(tr("ROI Y"), roiYSpin_);
    layout->addRow(tr("ROI Width"), roiWidthSpin_);
    layout->addRow(tr("ROI Height"), roiHeightSpin_);

    // Update ROI limits and load current values
    updateRoiLimits();

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    layout->addRow(buttons_);

    connect(buttons_, &QDialogButtonBox::accepted, this, [this]() {
        applySettings();
        accept();
    });
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons_->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this](bool) { onApply(); });
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
    roiXSpin_->setMaximum(std::max(0, maxWidth - 1));
    roiYSpin_->setMaximum(std::max(0, maxHeight - 1));
    roiWidthSpin_->setMaximum(maxWidth);
    roiHeightSpin_->setMaximum(maxHeight);

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
        roiXSpin_->setValue(currentRoi.x());
        roiYSpin_->setValue(currentRoi.y());
        roiWidthSpin_->setValue(currentRoi.width());
        roiHeightSpin_->setValue(currentRoi.height());
    } else {
        // Default to full image or clear
        roiXSpin_->setValue(0);
        roiYSpin_->setValue(0);
        roiWidthSpin_->setValue(maxWidth > 0 ? maxWidth : 1);
        roiHeightSpin_->setValue(maxHeight > 0 ? maxHeight : 1);
    }
}

void ProcessingSettingsDialog::applySettings() {
    auto& proc = backend_.processing();
    const int invalidNth = invalidSamplingSpin_->value();
    const int flushEvery = flushIntervalSpin_->value();

    proc.setInvalidFrameSamplingRate(static_cast<size_t>(invalidNth));
    proc.setFlushInterval(static_cast<size_t>(flushEvery));
    SPDLOG_INFO("Processing settings applied: invalidNth={}, flushEvery={}", invalidNth, flushEvery);

    // Apply ROI settings
    if (roiXSpin_ && roiYSpin_ && roiWidthSpin_ && roiHeightSpin_) {
        const int roiX = roiXSpin_->value();
        const int roiY = roiYSpin_->value();
        const int roiW = roiWidthSpin_->value();
        const int roiH = roiHeightSpin_->value();

        // Get current image dimensions for validation
        int maxWidth = roiWidthSpin_->maximum();
        int maxHeight = roiHeightSpin_->maximum();

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
}



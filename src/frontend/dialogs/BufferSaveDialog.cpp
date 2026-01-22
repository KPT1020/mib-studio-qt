#include "frontend/dialogs/BufferSaveDialog.h"
#include "ui_BufferSaveDialog.h"

#include <QCoreApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QDir>

#include <climits>
#include <algorithm>
#include <functional>

#include <spdlog/spdlog.h>

#include "backend/AppBackend.h"
#include "backend/services/PlaybackService.h"
#include "backend/services/ProcessingService.h"
#include "backend/playback/FrameStore.h"
#include "backend/Tools.h"

namespace frontend {

BufferSaveDialog::BufferSaveDialog(backend::AppBackend& backend, QWidget* parent)
    : QDialog(parent), ui(new Ui::BufferSaveDialog), backend_(backend) {
    ui->setupUi(this);

    // Enable/disable spin boxes based on radio selection
    ui->startIndexSpin->setEnabled(false);
    ui->endIndexSpin->setEnabled(false);
    ui->startTimestampSpin->setEnabled(false);
    ui->endTimestampSpin->setEnabled(false);

    // Connect signals
    connect(ui->browseBtn, &QPushButton::clicked, this, &BufferSaveDialog::onBrowseDirectory);
    connect(ui->allFramesRadio, &QRadioButton::toggled, this, &BufferSaveDialog::onRangeModeChanged);
    connect(ui->indexRangeRadio, &QRadioButton::toggled, this, &BufferSaveDialog::onRangeModeChanged);
    connect(ui->timestampRangeRadio, &QRadioButton::toggled, this, &BufferSaveDialog::onRangeModeChanged);
    connect(ui->refreshRangesBtn, &QPushButton::clicked, this, &BufferSaveDialog::onRefreshRanges);
    connect(ui->applyResizeBtn, &QPushButton::clicked, this, &BufferSaveDialog::onApplyResize);
    connect(ui->newCapacitySpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &BufferSaveDialog::updateMemoryDisplay);
    
    QPushButton* saveBtn = ui->buttons->button(QDialogButtonBox::Save);
    if (saveBtn) {
        saveBtn->setText(tr("Save Frames"));
    }
    connect(ui->buttons, &QDialogButtonBox::accepted, this, &BufferSaveDialog::onSaveFrames);
    connect(ui->buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Set default output directory using QStandardPaths for portability
    QString defaultDir;
    const QString documentsPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (!documentsPath.isEmpty()) {
        defaultDir = QDir(documentsPath).absoluteFilePath("MIB_Studio_Qt/saved_frames");
    } else {
        // Fallback to application directory if DocumentsLocation is unavailable
        const QString appDir = QCoreApplication::applicationDirPath();
        defaultDir = QDir(appDir).absoluteFilePath("saved_frames");
    }
    ui->outputDirEdit->setText(QDir(defaultDir).absolutePath());

    // Initial update
    updateAvailableRanges();
    updateUIState();
}

BufferSaveDialog::~BufferSaveDialog() {
    delete ui;
}

void BufferSaveDialog::onBrowseDirectory() {
    const QString current = ui->outputDirEdit->text().trimmed();
    QString selected = QFileDialog::getExistingDirectory(this,
                                                         tr("Select output directory"),
                                                         current.isEmpty() ? QDir::currentPath() : current);
    if (!selected.isEmpty()) {
        ui->outputDirEdit->setText(QDir(selected).absolutePath());
    }
}

void BufferSaveDialog::onRangeModeChanged() {
    const bool indexMode = ui->indexRangeRadio->isChecked();
    const bool timestampMode = ui->timestampRangeRadio->isChecked();

    ui->startIndexSpin->setEnabled(indexMode);
    ui->endIndexSpin->setEnabled(indexMode);
    ui->startTimestampSpin->setEnabled(timestampMode);
    ui->endTimestampSpin->setEnabled(timestampMode);

    updateUIState();
}

void BufferSaveDialog::onRefreshRanges() {
    updateAvailableRanges();
}

void BufferSaveDialog::onApplyResize() {
    const size_t newCapacity = static_cast<size_t>(ui->newCapacitySpin->value());
    const size_t currentCapacity = backend_.playback().capacity();

    if (newCapacity == currentCapacity) {
        QMessageBox::information(this, tr("Resize Buffer"), tr("New capacity is the same as current capacity."));
        return;
    }

    // Get memory information for validation
    const double currentProcessMemoryMB = backend::Tools::getProcessMemoryMB();
    const uint64_t availableSystemRAMBytes = backend::Tools::getAvailableSystemRAMBytes();
    const double availableSystemRAMMB = static_cast<double>(availableSystemRAMBytes) / (1024.0 * 1024.0);
    
    // Get FrameStore to estimate memory
    auto& playback = backend_.playback();
    const size_t estimatedBufferMemoryBytes = playback.estimateMemoryBytesForCapacity(newCapacity);
    const double estimatedBufferMemoryMB = static_cast<double>(estimatedBufferMemoryBytes) / (1024.0 * 1024.0);
    const double totalMemoryAfterResizeMB = currentProcessMemoryMB + estimatedBufferMemoryMB;
    const double thresholdMB = availableSystemRAMMB * 0.75;

    // Log validation check
    SPDLOG_DEBUG("BufferSaveDialog: Validation check - requested capacity: {}, current process memory: {:.2f} MB, estimated buffer memory: {:.2f} MB, total after resize: {:.2f} MB, available system RAM: {:.2f} MB, threshold (75%): {:.2f} MB",
                 newCapacity, currentProcessMemoryMB, estimatedBufferMemoryMB, totalMemoryAfterResizeMB, availableSystemRAMMB, thresholdMB);

    // Validate against available RAM (75% threshold)
    if (totalMemoryAfterResizeMB > thresholdMB) {
        // Calculate maximum recommended capacity
        const double availableForBufferMB = thresholdMB - currentProcessMemoryMB;
        size_t maxRecommendedCapacity = 0;
        if (availableForBufferMB > 0 && newCapacity > 0) {
            const double memoryPerFrameMB = estimatedBufferMemoryMB / static_cast<double>(newCapacity);
            if (memoryPerFrameMB > 0) {
                maxRecommendedCapacity = static_cast<size_t>(availableForBufferMB / memoryPerFrameMB);
            }
        }

        const QString message = tr(
            "Warning: The requested buffer size may exceed available system memory.\n\n"
            "Requested buffer capacity: %1 frames\n"
            "Current process memory: %2\n"
            "Estimated buffer memory: %3\n"
            "Total memory after resize: %4\n"
            "Available system RAM: %5\n"
            "Threshold (75%%): %6\n\n"
            "Maximum recommended buffer capacity: %7 frames\n\n"
            "Do you want to proceed anyway? This may cause system instability or allocation failures."
        )
        .arg(newCapacity)
        .arg(formatMemoryBytes(static_cast<uint64_t>(currentProcessMemoryMB * 1024 * 1024)))
        .arg(formatMemoryBytes(estimatedBufferMemoryBytes))
        .arg(formatMemoryBytes(static_cast<uint64_t>(totalMemoryAfterResizeMB * 1024 * 1024)))
        .arg(formatMemoryBytes(availableSystemRAMBytes))
        .arg(formatMemoryBytes(static_cast<uint64_t>(thresholdMB * 1024 * 1024)))
        .arg(maxRecommendedCapacity);

        SPDLOG_WARN("BufferSaveDialog: RAM validation failed - total memory ({:.2f} MB) would exceed threshold ({:.2f} MB)", totalMemoryAfterResizeMB, thresholdMB);

        const int reply = QMessageBox::warning(this,
                                               tr("Resize Buffer - Memory Warning"),
                                               message,
                                               QMessageBox::Yes | QMessageBox::No,
                                               QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            return;
        }
    } else {
        SPDLOG_INFO("BufferSaveDialog: RAM validation passed - total memory ({:.2f} MB) is within threshold ({:.2f} MB)", totalMemoryAfterResizeMB, thresholdMB);
    }

    // Warn if resize will clear frames
    uint64_t earliest = 0, latest = 0;
    size_t count = 0;
    bool hasFrames = playback.queryRange(earliest, latest, count);
    if (hasFrames && newCapacity < count) {
        const int reply = QMessageBox::warning(this,
                                               tr("Resize Buffer"),
                                               tr("Resizing to %1 will clear %2 frames. Continue?")
                                                   .arg(newCapacity)
                                                   .arg(count - newCapacity),
                                               QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            return;
        }
    }

    if (playback.resize(newCapacity)) {
        // Log successful resize
        const double finalProcessMemoryMB = backend::Tools::getProcessMemoryMB();
        const uint64_t finalAvailableRAMBytes = backend::Tools::getAvailableSystemRAMBytes();
        const double finalAvailableRAMMB = static_cast<double>(finalAvailableRAMBytes) / (1024.0 * 1024.0);
        const size_t finalEstimatedBufferMemoryBytes = playback.estimateMemoryBytesForCapacity(newCapacity);
        const double finalEstimatedBufferMemoryMB = static_cast<double>(finalEstimatedBufferMemoryBytes) / (1024.0 * 1024.0);

        SPDLOG_INFO("BufferSaveDialog: Buffer resized successfully - new capacity: {}, estimated buffer memory: {:.2f} MB, current process memory: {:.2f} MB, available system RAM: {:.2f} MB",
                    newCapacity, finalEstimatedBufferMemoryMB, finalProcessMemoryMB, finalAvailableRAMMB);

        QMessageBox::information(this, tr("Resize Buffer"), tr("Buffer resized successfully."));
        updateAvailableRanges();
        updateUIState();
    } else {
        SPDLOG_WARN("BufferSaveDialog: Failed to resize buffer to capacity {}", newCapacity);
        QMessageBox::warning(this, tr("Resize Buffer"), tr("Failed to resize buffer."));
    }
}

void BufferSaveDialog::onSaveFrames() {
    if (!validateInputs()) {
        return;
    }

    const QString outputDir = ui->outputDirEdit->text().trimmed();
    if (outputDir.isEmpty()) {
        QMessageBox::warning(this, tr("Save Frames"), tr("Please select an output directory."));
        return;
    }

    ui->statusLabel->setText(tr("Saving frames..."));
    ui->buttons->button(QDialogButtonBox::Save)->setEnabled(false);
    QCoreApplication::processEvents();

    bool success = false;
    std::string outputDirStd = outputDir.toStdString();

    // Create filter function if empty frame filtering is enabled
    std::function<bool(const backend::playback::Frame&)> filterFn = nullptr;
    if (ui->filterEmptyFramesCheck && ui->filterEmptyFramesCheck->isChecked()) {
        auto config = backend_.processing().getProcessingConfig();
        auto roi = backend_.processing().getRealtimeRoi();
        auto bg = backend_.processing().getRealtimeBackgroundGray();
        filterFn = [config, roi, bg](const backend::playback::Frame& frame) {
            return backend::services::ProcessingService::isFrameEmpty(frame, config, roi, bg);
        };
    }

    if (ui->allFramesRadio->isChecked()) {
        success = backend_.playback().saveFramesToDisk(outputDirStd, filterFn);
    } else if (ui->indexRangeRadio->isChecked()) {
        const uint64_t start = static_cast<uint64_t>(ui->startIndexSpin->value());
        const uint64_t end = static_cast<uint64_t>(ui->endIndexSpin->value());
        success = backend_.playback().saveFramesToDisk(outputDirStd, start, end, filterFn);
    } else if (ui->timestampRangeRadio->isChecked()) {
        const uint64_t start = static_cast<uint64_t>(ui->startTimestampSpin->value());
        const uint64_t end = static_cast<uint64_t>(ui->endTimestampSpin->value());
        success = backend_.playback().saveFramesToDisk(outputDirStd, start, end, true, filterFn);
    }

    ui->buttons->button(QDialogButtonBox::Save)->setEnabled(true);

    if (success) {
        ui->statusLabel->setText(tr("Frames saved successfully to: %1").arg(outputDir));
        QMessageBox::information(this, tr("Save Frames"), tr("Frames saved successfully."));
    } else {
        ui->statusLabel->setText(tr("Failed to save frames. Check logs for details."));
        QMessageBox::warning(this, tr("Save Frames"), tr("Failed to save frames. Check logs for details."));
    }
}

void BufferSaveDialog::onDialogAccepted() {
    // This slot is called when dialog is accepted
}

void BufferSaveDialog::updateAvailableRanges() {
    auto& playback = backend_.playback();

    // Update capacity
    const size_t capacity = playback.capacity();
    ui->currentCapacityValueLabel->setText(QString::number(capacity));
    ui->newCapacitySpin->setValue(static_cast<int>(capacity));

    // Log RAM usage when dialog opens
    const double currentProcessMemoryMB = backend::Tools::getProcessMemoryMB();
    const uint64_t availableSystemRAMBytes = backend::Tools::getAvailableSystemRAMBytes();
    const double availableSystemRAMMB = static_cast<double>(availableSystemRAMBytes) / (1024.0 * 1024.0);
    const size_t estimatedBufferMemoryBytes = playback.estimateMemoryBytesForCapacity(capacity);
    const double estimatedBufferMemoryMB = static_cast<double>(estimatedBufferMemoryBytes) / (1024.0 * 1024.0);

    SPDLOG_INFO("BufferSaveDialog: Dialog opened - current buffer capacity: {}, estimated buffer memory: {:.2f} MB, current process memory: {:.2f} MB, available system RAM: {:.2f} MB",
                capacity, estimatedBufferMemoryMB, currentProcessMemoryMB, availableSystemRAMMB);

    // Update index range
    uint64_t earliest = 0, latest = 0;
    size_t count = 0;
    if (playback.queryRange(earliest, latest, count)) {
        ui->availableRangeLabel->setText(tr("Available indices: %1 - %2 (%3 frames)")
                                         .arg(earliest)
                                         .arg(latest)
                                         .arg(count));
        // Clamp to int range for QSpinBox
        const int maxInt = INT_MAX;
        const int startVal = static_cast<int>(std::min(earliest, static_cast<uint64_t>(maxInt)));
        const int endVal = static_cast<int>(std::min(latest, static_cast<uint64_t>(maxInt)));
        ui->startIndexSpin->setRange(startVal, endVal);
        ui->endIndexSpin->setRange(startVal, endVal);
        ui->startIndexSpin->setValue(startVal);
        ui->endIndexSpin->setValue(endVal);
    } else {
        ui->availableRangeLabel->setText(tr("Available indices: No frames available"));
        ui->startIndexSpin->setRange(0, 0);
        ui->endIndexSpin->setRange(0, 0);
    }

    // Update timestamp range
    backend::playback::TimestampRange tsRange;
    if (playback.getAvailableTimestampRange(tsRange)) {
        ui->availableTimestampLabel->setText(tr("Available timestamps: %1 - %2")
                                             .arg(formatTimestamp(tsRange.start))
                                             .arg(formatTimestamp(tsRange.end)));
        // Clamp to int range for QSpinBox
        const int maxInt = INT_MAX;
        const int startVal = static_cast<int>(std::min(tsRange.start, static_cast<uint64_t>(maxInt)));
        const int endVal = static_cast<int>(std::min(tsRange.end, static_cast<uint64_t>(maxInt)));
        ui->startTimestampSpin->setRange(startVal, endVal);
        ui->endTimestampSpin->setRange(startVal, endVal);
        ui->startTimestampSpin->setValue(startVal);
        ui->endTimestampSpin->setValue(endVal);
    } else {
        ui->availableTimestampLabel->setText(tr("Available timestamps: No frames available"));
        ui->startTimestampSpin->setRange(0, 0);
        ui->endTimestampSpin->setRange(0, 0);
    }

    // Update memory display
    updateMemoryDisplay();
}

void BufferSaveDialog::updateUIState() {
    // Enable/disable save button based on state
    const bool hasOutputDir = !ui->outputDirEdit->text().trimmed().isEmpty();
    const bool hasFrames = backend_.playback().capacity() > 0;

    QPushButton* saveBtn = ui->buttons->button(QDialogButtonBox::Save);
    if (saveBtn) {
        saveBtn->setEnabled(hasOutputDir && hasFrames);
    }
}

bool BufferSaveDialog::validateInputs() {
    if (ui->indexRangeRadio->isChecked()) {
        const int start = ui->startIndexSpin->value();
        const int end = ui->endIndexSpin->value();
        if (start > end) {
            QMessageBox::warning(this, tr("Validation Error"), tr("Start index must be <= end index."));
            return false;
        }
    } else if (ui->timestampRangeRadio->isChecked()) {
        const int start = ui->startTimestampSpin->value();
        const int end = ui->endTimestampSpin->value();
        if (start > end) {
            QMessageBox::warning(this, tr("Validation Error"), tr("Start timestamp must be <= end timestamp."));
            return false;
        }
    }
    return true;
}

QString BufferSaveDialog::formatTimestamp(uint64_t timestampNs) const {
    // Format timestamp - if it's in nanoseconds, convert to microseconds for display
    // Otherwise display as-is
    if (timestampNs > 1000000000ULL) {
        // Likely nanoseconds, convert to microseconds
        return QString::number(timestampNs / 1000);
    }
    return QString::number(timestampNs);
}

QString BufferSaveDialog::formatMemoryBytes(uint64_t bytes) const {
    const double bytesDouble = static_cast<double>(bytes);
    if (bytesDouble >= 1024.0 * 1024.0 * 1024.0) {
        // GB
        return QString::number(bytesDouble / (1024.0 * 1024.0 * 1024.0), 'f', 2) + " GB";
    } else {
        // MB
        return QString::number(bytesDouble / (1024.0 * 1024.0), 'f', 2) + " MB";
    }
}

void BufferSaveDialog::updateMemoryDisplay() {
    const size_t newCapacity = static_cast<size_t>(ui->newCapacitySpin->value());
    auto& playback = backend_.playback();
    const size_t estimatedMemoryBytes = playback.estimateMemoryBytesForCapacity(newCapacity);
    ui->estimatedMemoryLabel->setText(tr("Estimated memory: %1").arg(formatMemoryBytes(estimatedMemoryBytes)));
}

} // namespace frontend


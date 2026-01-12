#include "frontend/BufferSaveDialog.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QStandardPaths>
#include <QDir>
#include <QCheckBox>

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
    : QDialog(parent), backend_(backend) {
    setWindowTitle(tr("Save Buffer & Manage Size"));
    setModal(true);
    resize(500, 400);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setSpacing(8);

    // Output directory section
    auto* dirGroup = new QGroupBox(tr("Output Directory"), this);
    auto* dirLayout = new QVBoxLayout(dirGroup);
    auto* dirRowLayout = new QHBoxLayout();
    outputDirEdit_ = new QLineEdit(this);
    outputDirEdit_->setPlaceholderText(tr("Select output directory for saved frames"));
    browseBtn_ = new QPushButton(tr("Browse..."), this);
    dirRowLayout->addWidget(outputDirEdit_);
    dirRowLayout->addWidget(browseBtn_);
    dirLayout->addLayout(dirRowLayout);
    rootLayout->addWidget(dirGroup);

    connect(browseBtn_, &QPushButton::clicked, this, &BufferSaveDialog::onBrowseDirectory);

    // Range selection section
    rangeGroup_ = new QGroupBox(tr("Frame Range"), this);
    auto* rangeLayout = new QVBoxLayout(rangeGroup_);

    allFramesRadio_ = new QRadioButton(tr("All available frames"), this);
    indexRangeRadio_ = new QRadioButton(tr("Index range"), this);
    timestampRangeRadio_ = new QRadioButton(tr("Timestamp range"), this);
    allFramesRadio_->setChecked(true);

    rangeLayout->addWidget(allFramesRadio_);
    rangeLayout->addWidget(indexRangeRadio_);

    auto* indexRangeLayout = new QHBoxLayout();
    startIndexSpin_ = new QSpinBox(this);
    startIndexSpin_->setRange(0, INT_MAX);
    startIndexSpin_->setValue(0);
    startIndexSpin_->setEnabled(false);
    endIndexSpin_ = new QSpinBox(this);
    endIndexSpin_->setRange(0, INT_MAX);
    endIndexSpin_->setValue(0);
    endIndexSpin_->setEnabled(false);
    indexRangeLayout->addWidget(new QLabel(tr("Start:"), this));
    indexRangeLayout->addWidget(startIndexSpin_);
    indexRangeLayout->addWidget(new QLabel(tr("End:"), this));
    indexRangeLayout->addWidget(endIndexSpin_);
    indexRangeLayout->addStretch();
    rangeLayout->addLayout(indexRangeLayout);

    rangeLayout->addWidget(timestampRangeRadio_);

    auto* timestampRangeLayout = new QHBoxLayout();
    startTimestampSpin_ = new QSpinBox(this);
    startTimestampSpin_->setRange(0, INT_MAX);
    startTimestampSpin_->setValue(0);
    startTimestampSpin_->setEnabled(false);
    endTimestampSpin_ = new QSpinBox(this);
    endTimestampSpin_->setRange(0, INT_MAX);
    endTimestampSpin_->setValue(0);
    endTimestampSpin_->setEnabled(false);
    timestampRangeLayout->addWidget(new QLabel(tr("Start:"), this));
    timestampRangeLayout->addWidget(startTimestampSpin_);
    timestampRangeLayout->addWidget(new QLabel(tr("End:"), this));
    timestampRangeLayout->addWidget(endTimestampSpin_);
    timestampRangeLayout->addStretch();
    rangeLayout->addLayout(timestampRangeLayout);

    availableRangeLabel_ = new QLabel(tr("Available: N/A"), this);
    availableTimestampLabel_ = new QLabel(tr("Available: N/A"), this);
    rangeLayout->addWidget(availableRangeLabel_);
    rangeLayout->addWidget(availableTimestampLabel_);

    refreshRangesBtn_ = new QPushButton(tr("Refresh Ranges"), this);
    rangeLayout->addWidget(refreshRangesBtn_);

    rootLayout->addWidget(rangeGroup_);

    connect(allFramesRadio_, &QRadioButton::toggled, this, &BufferSaveDialog::onRangeModeChanged);
    connect(indexRangeRadio_, &QRadioButton::toggled, this, &BufferSaveDialog::onRangeModeChanged);
    connect(timestampRangeRadio_, &QRadioButton::toggled, this, &BufferSaveDialog::onRangeModeChanged);
    connect(refreshRangesBtn_, &QPushButton::clicked, this, &BufferSaveDialog::onRefreshRanges);

    // Buffer size section
    bufferSizeGroup_ = new QGroupBox(tr("Buffer Size"), this);
    auto* bufferLayout = new QFormLayout(bufferSizeGroup_);
    currentCapacityLabel_ = new QLabel(tr("N/A"), this);
    bufferLayout->addRow(tr("Current capacity:"), currentCapacityLabel_);
    newCapacitySpin_ = new QSpinBox(this);
    newCapacitySpin_->setRange(1, 100000);
    newCapacitySpin_->setValue(512);
    bufferLayout->addRow(tr("New capacity:"), newCapacitySpin_);
    estimatedMemoryLabel_ = new QLabel(tr("Estimated memory: N/A"), this);
    bufferLayout->addRow(tr(""), estimatedMemoryLabel_);
    applyResizeBtn_ = new QPushButton(tr("Apply Resize"), this);
    bufferLayout->addRow(tr(""), applyResizeBtn_);
    rootLayout->addWidget(bufferSizeGroup_);

    connect(applyResizeBtn_, &QPushButton::clicked, this, &BufferSaveDialog::onApplyResize);
    connect(newCapacitySpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &BufferSaveDialog::updateMemoryDisplay);

    // Filter options section
    auto* filterGroup = new QGroupBox(tr("Filter Options"), this);
    auto* filterLayout = new QVBoxLayout(filterGroup);
    filterEmptyFramesCheck_ = new QCheckBox(tr("Remove empty frames"), this);
    filterEmptyFramesCheck_->setToolTip(tr("Skip frames with pixel count below threshold after binary threshold"));
    filterEmptyFramesCheck_->setChecked(false);
    filterLayout->addWidget(filterEmptyFramesCheck_);
    rootLayout->addWidget(filterGroup);

    // Status label
    statusLabel_ = new QLabel(tr("Ready"), this);
    statusLabel_->setWordWrap(true);
    rootLayout->addWidget(statusLabel_);

    // Dialog buttons
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    saveBtn_ = buttons->button(QDialogButtonBox::Save);
    if (saveBtn_) {
        saveBtn_->setText(tr("Save Frames"));
    }
    connect(buttons, &QDialogButtonBox::accepted, this, &BufferSaveDialog::onSaveFrames);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rootLayout->addWidget(buttons);

    // Set default output directory
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString defaultDir = QDir(appDir).absoluteFilePath("../data/saved_frames");
    outputDirEdit_->setText(QDir(defaultDir).absolutePath());

    // Initial update
    updateAvailableRanges();
    updateUIState();
}

void BufferSaveDialog::onBrowseDirectory() {
    const QString current = outputDirEdit_->text().trimmed();
    QString selected = QFileDialog::getExistingDirectory(this,
                                                         tr("Select output directory"),
                                                         current.isEmpty() ? QDir::currentPath() : current);
    if (!selected.isEmpty()) {
        outputDirEdit_->setText(QDir(selected).absolutePath());
    }
}

void BufferSaveDialog::onRangeModeChanged() {
    const bool indexMode = indexRangeRadio_->isChecked();
    const bool timestampMode = timestampRangeRadio_->isChecked();

    startIndexSpin_->setEnabled(indexMode);
    endIndexSpin_->setEnabled(indexMode);
    startTimestampSpin_->setEnabled(timestampMode);
    endTimestampSpin_->setEnabled(timestampMode);

    updateUIState();
}

void BufferSaveDialog::onRefreshRanges() {
    updateAvailableRanges();
}

void BufferSaveDialog::onApplyResize() {
    const size_t newCapacity = static_cast<size_t>(newCapacitySpin_->value());
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

    const QString outputDir = outputDirEdit_->text().trimmed();
    if (outputDir.isEmpty()) {
        QMessageBox::warning(this, tr("Save Frames"), tr("Please select an output directory."));
        return;
    }

    statusLabel_->setText(tr("Saving frames..."));
    saveBtn_->setEnabled(false);
    QCoreApplication::processEvents();

    bool success = false;
    std::string outputDirStd = outputDir.toStdString();

    // Create filter function if empty frame filtering is enabled
    std::function<bool(const backend::playback::Frame&)> filterFn = nullptr;
    if (filterEmptyFramesCheck_ && filterEmptyFramesCheck_->isChecked()) {
        auto config = backend_.processing().getProcessingConfig();
        auto roi = backend_.processing().getRealtimeRoi();
        auto bg = backend_.processing().getRealtimeBackgroundGray();
        filterFn = [config, roi, bg](const backend::playback::Frame& frame) {
            return backend::services::ProcessingService::isFrameEmpty(frame, config, roi, bg);
        };
    }

    if (allFramesRadio_->isChecked()) {
        success = backend_.playback().saveFramesToDisk(outputDirStd, filterFn);
    } else if (indexRangeRadio_->isChecked()) {
        const uint64_t start = static_cast<uint64_t>(startIndexSpin_->value());
        const uint64_t end = static_cast<uint64_t>(endIndexSpin_->value());
        success = backend_.playback().saveFramesToDisk(outputDirStd, start, end, filterFn);
    } else if (timestampRangeRadio_->isChecked()) {
        const uint64_t start = static_cast<uint64_t>(startTimestampSpin_->value());
        const uint64_t end = static_cast<uint64_t>(endTimestampSpin_->value());
        success = backend_.playback().saveFramesToDisk(outputDirStd, start, end, true, filterFn);
    }

    saveBtn_->setEnabled(true);

    if (success) {
        statusLabel_->setText(tr("Frames saved successfully to: %1").arg(outputDir));
        QMessageBox::information(this, tr("Save Frames"), tr("Frames saved successfully."));
    } else {
        statusLabel_->setText(tr("Failed to save frames. Check logs for details."));
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
    currentCapacityLabel_->setText(QString::number(capacity));
    newCapacitySpin_->setValue(static_cast<int>(capacity));

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
        availableRangeLabel_->setText(tr("Available indices: %1 - %2 (%3 frames)")
                                         .arg(earliest)
                                         .arg(latest)
                                         .arg(count));
        // Clamp to int range for QSpinBox
        const int maxInt = INT_MAX;
        const int startVal = static_cast<int>(std::min(earliest, static_cast<uint64_t>(maxInt)));
        const int endVal = static_cast<int>(std::min(latest, static_cast<uint64_t>(maxInt)));
        startIndexSpin_->setRange(startVal, endVal);
        endIndexSpin_->setRange(startVal, endVal);
        startIndexSpin_->setValue(startVal);
        endIndexSpin_->setValue(endVal);
    } else {
        availableRangeLabel_->setText(tr("Available indices: No frames available"));
        startIndexSpin_->setRange(0, 0);
        endIndexSpin_->setRange(0, 0);
    }

    // Update timestamp range
    backend::playback::TimestampRange tsRange;
    if (playback.getAvailableTimestampRange(tsRange)) {
        availableTimestampLabel_->setText(tr("Available timestamps: %1 - %2")
                                             .arg(formatTimestamp(tsRange.start))
                                             .arg(formatTimestamp(tsRange.end)));
        // Clamp to int range for QSpinBox
        const int maxInt = INT_MAX;
        const int startVal = static_cast<int>(std::min(tsRange.start, static_cast<uint64_t>(maxInt)));
        const int endVal = static_cast<int>(std::min(tsRange.end, static_cast<uint64_t>(maxInt)));
        startTimestampSpin_->setRange(startVal, endVal);
        endTimestampSpin_->setRange(startVal, endVal);
        startTimestampSpin_->setValue(startVal);
        endTimestampSpin_->setValue(endVal);
    } else {
        availableTimestampLabel_->setText(tr("Available timestamps: No frames available"));
        startTimestampSpin_->setRange(0, 0);
        endTimestampSpin_->setRange(0, 0);
    }

    // Update memory display
    updateMemoryDisplay();
}

void BufferSaveDialog::updateUIState() {
    // Enable/disable save button based on state
    const bool hasOutputDir = !outputDirEdit_->text().trimmed().isEmpty();
    const bool hasFrames = backend_.playback().capacity() > 0;

    if (saveBtn_) {
        saveBtn_->setEnabled(hasOutputDir && hasFrames);
    }
}

bool BufferSaveDialog::validateInputs() {
    if (indexRangeRadio_->isChecked()) {
        const int start = startIndexSpin_->value();
        const int end = endIndexSpin_->value();
        if (start > end) {
            QMessageBox::warning(this, tr("Validation Error"), tr("Start index must be <= end index."));
            return false;
        }
    } else if (timestampRangeRadio_->isChecked()) {
        const int start = startTimestampSpin_->value();
        const int end = endTimestampSpin_->value();
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
    const size_t newCapacity = static_cast<size_t>(newCapacitySpin_->value());
    auto& playback = backend_.playback();
    const size_t estimatedMemoryBytes = playback.estimateMemoryBytesForCapacity(newCapacity);
    estimatedMemoryLabel_->setText(tr("Estimated memory: %1").arg(formatMemoryBytes(estimatedMemoryBytes)));
}

} // namespace frontend


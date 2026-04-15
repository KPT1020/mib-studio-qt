#include "frontend/dialogs/BatchMaskDialog.h"

#include "backend/AppBackend.h"
#include "backend/services/BatchMaskSources.h"
#include "backend/services/Hdf5Service.h"
#include "backend/services/ProcessingService.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <opencv2/core.hpp>

#include <spdlog/spdlog.h>

namespace frontend {

BatchMaskDialog::BatchMaskDialog(backend::AppBackend& backend,
                                 QString hdf5LoadedPath,
                                 QWidget* parent)
    : QDialog(parent), backend_(backend), hdf5LoadedPath_(std::move(hdf5LoadedPath)) {
    setWindowTitle(tr("Regenerate Masks from Stream Images"));
    resize(560, 520);
    buildUi();
    onSourceChanged();
}

BatchMaskDialog::~BatchMaskDialog() = default;

bool BatchMaskDialog::displayRequested() const {
    return displayCheck_ && displayCheck_->isChecked();
}

void BatchMaskDialog::buildUi() {
    auto* root = new QVBoxLayout(this);

    // --- Source group ---
    auto* srcGroup = new QGroupBox(tr("Input source"), this);
    auto* srcLayout = new QVBoxLayout(srcGroup);

    srcHdf5_ = new QRadioButton(tr("Current HDF5 frames (/valid_frames/images)"), srcGroup);
    srcHdf5_->setEnabled(!hdf5LoadedPath_.isEmpty());
    srcHdf5_->setChecked(!hdf5LoadedPath_.isEmpty());
    srcLayout->addWidget(srcHdf5_);

    auto* rangeRow = new QHBoxLayout();
    rangeRow->addSpacing(20);
    rangeRow->addWidget(new QLabel(tr("Start:")));
    startIdxSpin_ = new QSpinBox(srcGroup);
    startIdxSpin_->setRange(0, 1000000000);
    startIdxSpin_->setValue(0);
    rangeRow->addWidget(startIdxSpin_);
    rangeRow->addWidget(new QLabel(tr("Count:")));
    countSpin_ = new QSpinBox(srcGroup);
    countSpin_->setRange(1, 1000000);
    countSpin_->setValue(100);
    rangeRow->addWidget(countSpin_);
    rangeRow->addStretch();
    srcLayout->addLayout(rangeRow);

    srcFolder_ = new QRadioButton(tr("Folder of TIFF/PNG/JPEG images"), srcGroup);
    srcFolder_->setChecked(hdf5LoadedPath_.isEmpty());
    srcLayout->addWidget(srcFolder_);

    auto* folderRow = new QHBoxLayout();
    folderRow->addSpacing(20);
    folderEdit_ = new QLineEdit(srcGroup);
    folderRow->addWidget(folderEdit_);
    folderBrowseBtn_ = new QPushButton(tr("Browse..."), srcGroup);
    folderRow->addWidget(folderBrowseBtn_);
    srcLayout->addLayout(folderRow);

    root->addWidget(srcGroup);

    // --- Output group ---
    auto* outGroup = new QGroupBox(tr("Output"), this);
    auto* outLayout = new QVBoxLayout(outGroup);

    displayCheck_ = new QCheckBox(tr("Display results in review tab"), outGroup);
    displayCheck_->setChecked(true);
    outLayout->addWidget(displayCheck_);

    savePngCheck_ = new QCheckBox(tr("Save mask PNGs to directory"), outGroup);
    outLayout->addWidget(savePngCheck_);
    auto* pngRow = new QHBoxLayout();
    pngRow->addSpacing(20);
    pngDirEdit_ = new QLineEdit(outGroup);
    pngDirEdit_->setEnabled(false);
    pngRow->addWidget(pngDirEdit_);
    pngBrowseBtn_ = new QPushButton(tr("Browse..."), outGroup);
    pngBrowseBtn_->setEnabled(false);
    pngRow->addWidget(pngBrowseBtn_);
    outLayout->addLayout(pngRow);

    saveHdf5Check_ = new QCheckBox(tr("Save masks to HDF5 file"), outGroup);
    outLayout->addWidget(saveHdf5Check_);
    auto* h5Row = new QHBoxLayout();
    h5Row->addSpacing(20);
    hdf5PathEdit_ = new QLineEdit(outGroup);
    hdf5PathEdit_->setEnabled(false);
    h5Row->addWidget(hdf5PathEdit_);
    hdf5BrowseBtn_ = new QPushButton(tr("Browse..."), outGroup);
    hdf5BrowseBtn_->setEnabled(false);
    h5Row->addWidget(hdf5BrowseBtn_);
    outLayout->addLayout(h5Row);

    root->addWidget(outGroup);

    // --- Info: using current ProcessingConfig ---
    auto* cfgLabel = new QLabel(
        tr("Using the current ProcessingConfig from the live pipeline "
           "(edit it via the Processing settings tab before running)."),
        this);
    cfgLabel->setWordWrap(true);
    cfgLabel->setStyleSheet("color: gray;");
    root->addWidget(cfgLabel);

    // --- Status ---
    progressBar_ = new QProgressBar(this);
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    root->addWidget(progressBar_);

    statusLabel_ = new QLabel(tr("Idle."), this);
    root->addWidget(statusLabel_);

    logView_ = new QPlainTextEdit(this);
    logView_->setReadOnly(true);
    logView_->setMaximumHeight(120);
    root->addWidget(logView_);

    // --- Buttons ---
    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    runBtn_ = new QPushButton(tr("Run"), this);
    runBtn_->setDefault(true);
    btnRow->addWidget(runBtn_);
    closeBtn_ = new QPushButton(tr("Close"), this);
    btnRow->addWidget(closeBtn_);
    root->addLayout(btnRow);

    // --- Signals ---
    connect(srcHdf5_, &QRadioButton::toggled, this, &BatchMaskDialog::onSourceChanged);
    connect(srcFolder_, &QRadioButton::toggled, this, &BatchMaskDialog::onSourceChanged);
    connect(folderBrowseBtn_, &QPushButton::clicked, this, &BatchMaskDialog::onBrowseFolder);

    connect(savePngCheck_, &QCheckBox::toggled, pngDirEdit_, &QWidget::setEnabled);
    connect(savePngCheck_, &QCheckBox::toggled, pngBrowseBtn_, &QWidget::setEnabled);
    connect(pngBrowseBtn_, &QPushButton::clicked, this, &BatchMaskDialog::onBrowseOutputPng);

    connect(saveHdf5Check_, &QCheckBox::toggled, hdf5PathEdit_, &QWidget::setEnabled);
    connect(saveHdf5Check_, &QCheckBox::toggled, hdf5BrowseBtn_, &QWidget::setEnabled);
    connect(hdf5BrowseBtn_, &QPushButton::clicked, this, &BatchMaskDialog::onBrowseOutputHdf5);

    connect(runBtn_, &QPushButton::clicked, this, &BatchMaskDialog::onRun);
    connect(closeBtn_, &QPushButton::clicked, this, &QDialog::accept);
}

void BatchMaskDialog::onSourceChanged() {
    const bool hdf5 = srcHdf5_->isChecked();
    startIdxSpin_->setEnabled(hdf5);
    countSpin_->setEnabled(hdf5);
    folderEdit_->setEnabled(!hdf5);
    folderBrowseBtn_->setEnabled(!hdf5);
}

void BatchMaskDialog::onBrowseFolder() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select folder of stream images"),
        folderEdit_->text());
    if (!dir.isEmpty()) folderEdit_->setText(dir);
}

void BatchMaskDialog::onBrowseOutputPng() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select output directory for mask PNGs"),
        pngDirEdit_->text());
    if (!dir.isEmpty()) pngDirEdit_->setText(dir);
}

void BatchMaskDialog::onBrowseOutputHdf5() {
    const QString file = QFileDialog::getSaveFileName(
        this, tr("Save masks to HDF5 file"),
        hdf5PathEdit_->text(),
        tr("HDF5 files (*.h5 *.hdf5)"));
    if (!file.isEmpty()) hdf5PathEdit_->setText(file);
}

bool BatchMaskDialog::loadInputs(std::vector<cv::Mat>& outGray,
                                 std::vector<std::string>& outNames,
                                 QString& errorOut) {
    outGray.clear();
    outNames.clear();

    if (srcHdf5_->isChecked()) {
        if (hdf5LoadedPath_.isEmpty()) {
            errorOut = tr("No HDF5 file is currently loaded.");
            return false;
        }
        // The HdfReviewTab's reader is not exposed to us, so open a fresh
        // read-only reader for the batch operation.
        backend::services::Hdf5Service reader;
        if (!reader.loadFile(hdf5LoadedPath_.toStdString())) {
            errorOut = tr("Failed to open HDF5 file: %1").arg(hdf5LoadedPath_);
            return false;
        }
        const size_t start = static_cast<size_t>(startIdxSpin_->value());
        const size_t count = static_cast<size_t>(countSpin_->value());
        if (!backend::services::batch_masks::loadFromHdf5(
                reader, "/valid_frames/images", start, count, outGray)) {
            errorOut = tr("Failed to read images from HDF5 /valid_frames/images.");
            return false;
        }
        outNames.resize(outGray.size());
        for (size_t i = 0; i < outNames.size(); ++i) {
            outNames[i] = "frame_" + std::to_string(start + i);
        }
        return true;
    } else {
        const QString folder = folderEdit_->text().trimmed();
        if (folder.isEmpty()) {
            errorOut = tr("Please pick a folder.");
            return false;
        }
        std::vector<std::string> errors;
        if (!backend::services::batch_masks::loadFromFolder(
                folder.toStdString(), outGray, outNames, errors)) {
            errorOut = tr("Failed to load folder.");
            return false;
        }
        if (!errors.empty()) {
            for (const auto& e : errors) {
                logView_->appendPlainText(QString::fromStdString(e));
            }
        }
        if (outGray.empty()) {
            errorOut = tr("No supported images found in folder.");
            return false;
        }
        return true;
    }
}

void BatchMaskDialog::setRunning(bool running) {
    runBtn_->setEnabled(!running);
    srcHdf5_->setEnabled(!running && !hdf5LoadedPath_.isEmpty());
    srcFolder_->setEnabled(!running);
    folderBrowseBtn_->setEnabled(!running && srcFolder_->isChecked());
    savePngCheck_->setEnabled(!running);
    saveHdf5Check_->setEnabled(!running);
    displayCheck_->setEnabled(!running);
}

void BatchMaskDialog::onRun() {
    logView_->clear();
    results_.clear();
    statusLabel_->setText(tr("Loading images..."));
    progressBar_->setValue(0);
    setRunning(true);

    std::vector<cv::Mat> images;
    std::vector<std::string> names;
    QString err;
    if (!loadInputs(images, names, err)) {
        setRunning(false);
        statusLabel_->setText(tr("Error: %1").arg(err));
        QMessageBox::warning(this, tr("Load failed"), err);
        return;
    }
    logView_->appendPlainText(tr("Loaded %1 images.").arg(images.size()));

    // Run processing synchronously (processBatch is fast per-frame and the
    // UI stays responsive enough for typical batch sizes thanks to
    // QCoreApplication::processEvents in the progress callback).
    auto& proc = backend_.processing();
    const auto config = proc.getProcessingConfig();
    const auto roi = proc.getRealtimeRoi();
    const cv::Mat background = proc.getRealtimeBackgroundGray();

    progressBar_->setRange(0, static_cast<int>(images.size()));
    statusLabel_->setText(tr("Processing %1 images...").arg(images.size()));

    auto progressCb = [this](const backend::services::ProcessingService::BatchProgress& p) {
        progressBar_->setValue(static_cast<int>(p.done));
        if ((p.done % 25) == 0 || p.done == p.total) {
            QCoreApplication::processEvents();
        }
    };

    results_ = proc.processBatch(images, config, background, roi, progressCb);

    size_t validCount = 0;
    for (const auto& f : results_) if (f.validation.isValid) ++validCount;
    logView_->appendPlainText(
        tr("Processed %1 images: %2 valid, %3 invalid.")
            .arg(results_.size()).arg(validCount).arg(results_.size() - validCount));

    // Save outputs
    if (savePngCheck_->isChecked()) {
        const QString dir = pngDirEdit_->text().trimmed();
        if (dir.isEmpty()) {
            logView_->appendPlainText(tr("PNG output skipped: no directory chosen."));
        } else {
            size_t n = backend::services::batch_masks::saveMaskImages(
                results_, dir.toStdString(), names);
            logView_->appendPlainText(tr("Wrote %1 PNG masks to %2").arg(n).arg(dir));
        }
    }

    if (saveHdf5Check_->isChecked()) {
        const QString path = hdf5PathEdit_->text().trimmed();
        if (path.isEmpty()) {
            logView_->appendPlainText(tr("HDF5 output skipped: no path chosen."));
        } else {
            bool ok = backend::services::batch_masks::saveMasksToHdf5(
                results_, path.toStdString(), config,
                roi.x, roi.y, roi.w, roi.h, background);
            logView_->appendPlainText(
                ok ? tr("Wrote HDF5 to %1").arg(path)
                   : tr("HDF5 write FAILED for %1").arg(path));
        }
    }

    statusLabel_->setText(tr("Done."));
    setRunning(false);
}

} // namespace frontend

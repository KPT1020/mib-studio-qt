#include "frontend/dialogs/BatchMaskDialog.h"

#include "backend/AppBackend.h"
#include "backend/services/BatchMaskSources.h"
#include "backend/services/Hdf5Service.h"
#include "backend/services/ProcessingService.h"
#include "frontend/utils/RoiDrawCanvas.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
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
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QVBoxLayout>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <spdlog/spdlog.h>

namespace frontend {

BatchMaskDialog::BatchMaskDialog(backend::AppBackend& backend,
                                 QString hdf5LoadedPath,
                                 QWidget* parent)
    : QDialog(parent), backend_(backend), hdf5LoadedPath_(std::move(hdf5LoadedPath)) {
    setWindowTitle(tr("Regenerate Masks from Stream Images"));
    resize(1180, 540);
    buildUi();
    resetConfigToLive();
    onSourceChanged();
    onPreviewSourceChanged();
}

BatchMaskDialog::~BatchMaskDialog() = default;

void BatchMaskDialog::buildUi() {
    auto* root = new QVBoxLayout(this);

    // -----------------------------------------------------------------------
    // Top section: left controls + right preview
    // -----------------------------------------------------------------------
    auto* topRow  = new QHBoxLayout();
    auto* leftCol = new QVBoxLayout();

    // --- Source group ---
    auto* srcGroup  = new QGroupBox(tr("Input source"), this);
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

    srcAvi_ = new QRadioButton(tr("AVI video file"), srcGroup);
    srcLayout->addWidget(srcAvi_);

    auto* aviRow = new QHBoxLayout();
    aviRow->addSpacing(20);
    aviEdit_ = new QLineEdit(srcGroup);
    aviRow->addWidget(aviEdit_);
    aviBrowseBtn_ = new QPushButton(tr("Browse..."), srcGroup);
    aviRow->addWidget(aviBrowseBtn_);
    srcLayout->addLayout(aviRow);

    leftCol->addWidget(srcGroup);

    leftCol->addStretch();

    topRow->addLayout(leftCol, 1);

    // --- Preview & ROI group ---
    auto* previewGroup  = new QGroupBox(tr("Preview && ROI"), this);
    auto* previewLayout = new QVBoxLayout(previewGroup);

    roiCanvas_ = new RoiDrawCanvas(previewGroup);
    previewLayout->addWidget(roiCanvas_, 1);

    auto* navRow = new QHBoxLayout();
    prevFrameBtn_ = new QPushButton(tr("\u2190 Prev"), previewGroup);
    prevFrameBtn_->setEnabled(false);
    navRow->addWidget(prevFrameBtn_);
    frameCountLabel_ = new QLabel(tr("No frames"), previewGroup);
    frameCountLabel_->setAlignment(Qt::AlignCenter);
    navRow->addWidget(frameCountLabel_, 1);
    nextFrameBtn_ = new QPushButton(tr("Next \u2192"), previewGroup);
    nextFrameBtn_->setEnabled(false);
    navRow->addWidget(nextFrameBtn_);
    previewLayout->addLayout(navRow);

    auto* bgRow = new QHBoxLayout();
    setBgBtn_ = new QPushButton(tr("Set as Background"), previewGroup);
    bgRow->addWidget(setBgBtn_);
    clearBgBtn_ = new QPushButton(tr("Clear"), previewGroup);
    bgRow->addWidget(clearBgBtn_);
    bgStatusLabel_ = new QLabel(tr("Background: none"), previewGroup);
    bgStatusLabel_->setStyleSheet("color: gray;");
    bgRow->addWidget(bgStatusLabel_, 1);
    previewLayout->addLayout(bgRow);

    topRow->addWidget(previewGroup, 1);

    // --- Processing Config group (third column) ---
    auto* configGroup  = new QGroupBox(tr("Processing Config"), this);
    configGroup->setFixedWidth(230);
    auto* configLayout = new QVBoxLayout(configGroup);

    // Image Processing sub-group
    auto* imgGroup  = new QGroupBox(tr("Image Processing"), configGroup);
    auto* imgForm   = new QFormLayout(imgGroup);

    blurSpin_ = new QSpinBox(imgGroup);
    blurSpin_->setRange(1, 99);
    blurSpin_->setSingleStep(2);
    imgForm->addRow(tr("Blur kernel:"), blurSpin_);

    bgThreshSpin_ = new QSpinBox(imgGroup);
    bgThreshSpin_->setRange(0, 255);
    imgForm->addRow(tr("BG threshold:"), bgThreshSpin_);

    morphKernelSpin_ = new QSpinBox(imgGroup);
    morphKernelSpin_->setRange(1, 99);
    morphKernelSpin_->setSingleStep(2);
    imgForm->addRow(tr("Morph kernel:"), morphKernelSpin_);

    morphIterSpin_ = new QSpinBox(imgGroup);
    morphIterSpin_->setRange(1, 20);
    imgForm->addRow(tr("Morph iters:"), morphIterSpin_);

    configLayout->addWidget(imgGroup);

    // Validation sub-group
    auto* valGroup = new QGroupBox(tr("Validation"), configGroup);
    auto* valForm  = new QFormLayout(valGroup);

    areaMinSpin_ = new QSpinBox(valGroup);
    areaMinSpin_->setRange(0, 10000);
    areaMinSpin_->setSuffix(tr(" \u03bcm\u00b2"));
    valForm->addRow(tr("Area min:"), areaMinSpin_);

    areaMaxSpin_ = new QSpinBox(valGroup);
    areaMaxSpin_->setRange(0, 10000);
    areaMaxSpin_->setSuffix(tr(" \u03bcm\u00b2"));
    valForm->addRow(tr("Area max:"), areaMaxSpin_);

    deformMinSpin_ = new QDoubleSpinBox(valGroup);
    deformMinSpin_->setRange(0.0, 1.0);
    deformMinSpin_->setSingleStep(0.001);
    deformMinSpin_->setDecimals(4);
    valForm->addRow(tr("Deform min:"), deformMinSpin_);

    deformMaxSpin_ = new QDoubleSpinBox(valGroup);
    deformMaxSpin_->setRange(0.0, 1.0);
    deformMaxSpin_->setSingleStep(0.001);
    deformMaxSpin_->setDecimals(4);
    valForm->addRow(tr("Deform max:"), deformMaxSpin_);

    configLayout->addWidget(valGroup);

    auto* resetConfigBtn = new QPushButton(tr("Reset to live defaults"), configGroup);
    configLayout->addWidget(resetConfigBtn);
    configLayout->addStretch();

    topRow->addWidget(configGroup);

    root->addLayout(topRow);

    // -----------------------------------------------------------------------
    // Bottom section: info, progress, log, buttons
    // -----------------------------------------------------------------------
    auto* cfgLabel = new QLabel(
        tr("Processing parameters are local to this run. "
           "Use \u201cReset to live defaults\u201d to re-sync with the live pipeline."),
        this);
    cfgLabel->setWordWrap(true);
    cfgLabel->setStyleSheet("color: gray;");
    root->addWidget(cfgLabel);

    progressBar_ = new QProgressBar(this);
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    root->addWidget(progressBar_);

    statusLabel_ = new QLabel(tr("Idle."), this);
    root->addWidget(statusLabel_);

    logView_ = new QPlainTextEdit(this);
    logView_->setReadOnly(true);
    logView_->setMaximumHeight(100);
    root->addWidget(logView_);

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    runBtn_ = new QPushButton(tr("Run"), this);
    runBtn_->setDefault(true);
    btnRow->addWidget(runBtn_);
    closeBtn_ = new QPushButton(tr("Close"), this);
    btnRow->addWidget(closeBtn_);
    root->addLayout(btnRow);

    // -----------------------------------------------------------------------
    // Signals
    // -----------------------------------------------------------------------
    connect(srcHdf5_,    &QRadioButton::toggled, this, &BatchMaskDialog::onSourceChanged);
    connect(srcFolder_,  &QRadioButton::toggled, this, &BatchMaskDialog::onSourceChanged);
    connect(srcAvi_,     &QRadioButton::toggled, this, &BatchMaskDialog::onSourceChanged);
    connect(folderBrowseBtn_, &QPushButton::clicked, this, &BatchMaskDialog::onBrowseFolder);
    connect(aviBrowseBtn_,    &QPushButton::clicked, this, &BatchMaskDialog::onBrowseAvi);

    connect(runBtn_,   &QPushButton::clicked, this, &BatchMaskDialog::onRun);
    connect(closeBtn_, &QPushButton::clicked, this, &QDialog::reject);

    // Preview panel signals
    connect(srcHdf5_,  &QRadioButton::toggled,
            this, &BatchMaskDialog::onPreviewSourceChanged);
    connect(srcFolder_,&QRadioButton::toggled,
            this, &BatchMaskDialog::onPreviewSourceChanged);
    connect(srcAvi_,   &QRadioButton::toggled,
            this, &BatchMaskDialog::onPreviewSourceChanged);
    connect(folderEdit_, &QLineEdit::editingFinished,
            this, &BatchMaskDialog::onPreviewSourceChanged);
    connect(aviEdit_,    &QLineEdit::editingFinished,
            this, &BatchMaskDialog::onPreviewSourceChanged);
    connect(startIdxSpin_, qOverload<int>(&QSpinBox::valueChanged),
            this, &BatchMaskDialog::onPreviewSourceChanged);

    connect(prevFrameBtn_,  &QPushButton::clicked, this, &BatchMaskDialog::onPrevFrame);
    connect(nextFrameBtn_,  &QPushButton::clicked, this, &BatchMaskDialog::onNextFrame);
    connect(setBgBtn_,      &QPushButton::clicked, this, &BatchMaskDialog::onSetBackground);
    connect(clearBgBtn_,    &QPushButton::clicked, this, &BatchMaskDialog::onClearBackground);

    // Config spinbox → localConfig_ live update
    // Blur and morph kernels must be odd (pipeline calls toOdd() internally,
    // but we enforce it here so the displayed value matches what runs).
    auto toOdd = [](int v){ return (v % 2 == 0) ? v + 1 : v; };
    connect(blurSpin_,       qOverload<int>(&QSpinBox::valueChanged),
            this, [this, toOdd](int v){ localConfig_.gaussian_blur_size = toOdd(v); });
    connect(bgThreshSpin_,   qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int v){ localConfig_.bg_subtract_threshold = v; });
    connect(morphKernelSpin_,qOverload<int>(&QSpinBox::valueChanged),
            this, [this, toOdd](int v){ localConfig_.morph_kernel_size = toOdd(v); });
    connect(morphIterSpin_,  qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int v){ localConfig_.morph_iterations = v; });
    connect(areaMinSpin_,    qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int v){ localConfig_.area_threshold_min = v; });
    connect(areaMaxSpin_,    qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int v){ localConfig_.area_threshold_max = v; });
    connect(deformMinSpin_,  qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v){ localConfig_.deformability_threshold_min = v; });
    connect(deformMaxSpin_,  qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v){ localConfig_.deformability_threshold_max = v; });
    connect(resetConfigBtn,  &QPushButton::clicked,
            this, &BatchMaskDialog::resetConfigToLive);
}

// ---------------------------------------------------------------------------
// Source / browse slots (unchanged from original)
// ---------------------------------------------------------------------------

void BatchMaskDialog::onSourceChanged() {
    const bool hdf5   = srcHdf5_->isChecked();
    const bool folder = srcFolder_->isChecked();
    const bool avi    = srcAvi_->isChecked();
    startIdxSpin_->setEnabled(hdf5);
    countSpin_->setEnabled(hdf5);
    folderEdit_->setEnabled(folder);
    folderBrowseBtn_->setEnabled(folder);
    aviEdit_->setEnabled(avi);
    aviBrowseBtn_->setEnabled(avi);
}

void BatchMaskDialog::onBrowseFolder() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select folder of stream images"),
        folderEdit_->text());
    if (!dir.isEmpty()) {
        folderEdit_->setText(dir);
        onPreviewSourceChanged();
    }
}

void BatchMaskDialog::onBrowseAvi() {
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Select AVI file"),
        aviEdit_->text(),
        tr("AVI Video (*.avi);;All Files (*)"));
    if (!file.isEmpty()) {
        aviEdit_->setText(file);
        onPreviewSourceChanged();
    }
}

// ---------------------------------------------------------------------------
// Preview panel: source changed
// ---------------------------------------------------------------------------

void BatchMaskDialog::onPreviewSourceChanged() {
    backgroundMat_ = cv::Mat{};
    bgStatusLabel_->setText(tr("Background: none"));

    previewFrameTotal_ = getSourceFrameCount();
    previewFrameIndex_ = 0;

    // Pre-populate ROI from HDF5 experiment_info when possible
    if (srcHdf5_->isChecked() && !hdf5LoadedPath_.isEmpty()) {
        backend::services::Hdf5Service reader;
        if (reader.loadFile(hdf5LoadedPath_.toStdString())) {
            uint64_t s{}, e{};
            size_t v{}, inv{};
            backend::services::ProcessingService::Roi roi{};
            if (reader.readExperimentInfo(s, e, v, inv, &roi) && roi.w > 0 && roi.h > 0) {
                roiCanvas_->setRoi(QRect(roi.x, roi.y, roi.w, roi.h));
            }
        }
    }

    loadPreviewFrame(0);
}

// ---------------------------------------------------------------------------
// Preview panel: frame navigation
// ---------------------------------------------------------------------------

int BatchMaskDialog::getSourceFrameCount() const {
    if (srcHdf5_->isChecked()) {
        if (hdf5LoadedPath_.isEmpty()) return 0;
        backend::services::Hdf5Service reader;
        if (!reader.loadFile(hdf5LoadedPath_.toStdString())) return 0;
        size_t count = 0;
        int h = 0, w = 0, ch = 0;
        if (!reader.getDatasetInfo("/valid_frames/images", count, h, w, ch)) return 0;
        const size_t start = static_cast<size_t>(startIdxSpin_->value());
        if (start >= count) return 0;
        const size_t requested = static_cast<size_t>(countSpin_->value());
        return static_cast<int>(std::min(requested, count - start));
    } else if (srcAvi_->isChecked()) {
        const QString path = aviEdit_->text().trimmed();
        if (path.isEmpty() || !QFileInfo(path).isFile()) return 0;

        // Reuse cached cap if same path; otherwise open.
        if (path != previewAviPath_ || !previewAviCap_.isOpened()) {
            if (previewAviCap_.isOpened()) previewAviCap_.release();
            if (!previewAviCap_.open(path.toStdString())) {
                previewAviPath_.clear();
                previewAviTotal_ = 0;
                return 0;
            }
            previewAviPath_ = path;
            previewAviTotal_ = static_cast<int>(
                previewAviCap_.get(cv::CAP_PROP_FRAME_COUNT));
            if (previewAviTotal_ < 0) previewAviTotal_ = 0;
        }
        return previewAviTotal_;
    } else {
        const QString folder = folderEdit_->text().trimmed();
        if (folder.isEmpty()) return 0;
        QDir dir(folder);
        QStringList files = dir.entryList(
            {"*.tif", "*.tiff", "*.png", "*.jpg", "*.jpeg"},
            QDir::Files, QDir::Name);
        return files.size();
    }
}

void BatchMaskDialog::loadPreviewFrame(int index) {
    if (previewFrameTotal_ <= 0) {
        roiCanvas_->setImage(QImage{});
        frameCountLabel_->setText(tr("No frames"));
        prevFrameBtn_->setEnabled(false);
        nextFrameBtn_->setEnabled(false);
        return;
    }

    previewFrameIndex_ = qBound(0, index, previewFrameTotal_ - 1);
    frameCountLabel_->setText(
        tr("Frame %1 / %2").arg(previewFrameIndex_ + 1).arg(previewFrameTotal_));
    prevFrameBtn_->setEnabled(previewFrameIndex_ > 0);
    nextFrameBtn_->setEnabled(previewFrameIndex_ < previewFrameTotal_ - 1);

    cv::Mat mat;
    if (srcHdf5_->isChecked()) {
        backend::services::Hdf5Service reader;
        if (reader.loadFile(hdf5LoadedPath_.toStdString())) {
            const size_t absIdx =
                static_cast<size_t>(startIdxSpin_->value()) + previewFrameIndex_;
            reader.readImageByIndex("/valid_frames/images", absIdx, mat);
        }
    } else if (srcAvi_->isChecked()) {
        if (previewAviCap_.isOpened()) {
            previewAviCap_.set(cv::CAP_PROP_POS_FRAMES,
                               static_cast<double>(previewFrameIndex_));
            cv::Mat raw;
            if (previewAviCap_.read(raw) && !raw.empty()) {
                if (raw.channels() == 1) {
                    mat = raw.clone();
                    if (mat.type() != CV_8UC1) {
                        cv::Mat tmp;
                        mat.convertTo(tmp, CV_8UC1);
                        mat = std::move(tmp);
                    }
                } else if (raw.channels() == 3) {
                    cv::cvtColor(raw, mat, cv::COLOR_BGR2GRAY);
                } else if (raw.channels() == 4) {
                    cv::cvtColor(raw, mat, cv::COLOR_BGRA2GRAY);
                }
            }
        }
    } else {
        QDir dir(folderEdit_->text().trimmed());
        QStringList files = dir.entryList(
            {"*.tif", "*.tiff", "*.png", "*.jpg", "*.jpeg"},
            QDir::Files, QDir::Name);
        if (previewFrameIndex_ < files.size()) {
            mat = cv::imread(
                dir.filePath(files[previewFrameIndex_]).toStdString(),
                cv::IMREAD_GRAYSCALE);
        }
    }

    roiCanvas_->setImage(matToQImage(mat));
}

void BatchMaskDialog::onPrevFrame() { loadPreviewFrame(previewFrameIndex_ - 1); }
void BatchMaskDialog::onNextFrame() { loadPreviewFrame(previewFrameIndex_ + 1); }

// ---------------------------------------------------------------------------
// Preview panel: background selection
// ---------------------------------------------------------------------------

void BatchMaskDialog::onSetBackground() {
    cv::Mat mat;
    if (srcHdf5_->isChecked()) {
        backend::services::Hdf5Service reader;
        if (reader.loadFile(hdf5LoadedPath_.toStdString())) {
            const size_t absIdx =
                static_cast<size_t>(startIdxSpin_->value()) + previewFrameIndex_;
            reader.readImageByIndex("/valid_frames/images", absIdx, mat);
        }
    } else if (srcAvi_->isChecked()) {
        if (previewAviCap_.isOpened()) {
            previewAviCap_.set(cv::CAP_PROP_POS_FRAMES,
                               static_cast<double>(previewFrameIndex_));
            cv::Mat raw;
            if (previewAviCap_.read(raw) && !raw.empty()) {
                if (raw.channels() == 1) {
                    mat = raw.clone();
                } else if (raw.channels() == 3) {
                    cv::cvtColor(raw, mat, cv::COLOR_BGR2GRAY);
                } else if (raw.channels() == 4) {
                    cv::cvtColor(raw, mat, cv::COLOR_BGRA2GRAY);
                }
            }
        }
    } else {
        QDir dir(folderEdit_->text().trimmed());
        QStringList files = dir.entryList(
            {"*.tif", "*.tiff", "*.png", "*.jpg", "*.jpeg"},
            QDir::Files, QDir::Name);
        if (previewFrameIndex_ < files.size()) {
            mat = cv::imread(
                dir.filePath(files[previewFrameIndex_]).toStdString(),
                cv::IMREAD_GRAYSCALE);
        }
    }

    if (mat.empty()) {
        spdlog::warn("BatchMaskDialog: could not load frame {} as background",
                     previewFrameIndex_);
        return;
    }
    backgroundMat_ = mat;
    bgStatusLabel_->setText(tr("Background: Frame %1").arg(previewFrameIndex_ + 1));
}

void BatchMaskDialog::onClearBackground() {
    backgroundMat_ = cv::Mat{};
    bgStatusLabel_->setText(tr("Background: none"));
}

void BatchMaskDialog::resetConfigToLive() {
    localConfig_ = backend_.processing().getProcessingConfig();
    const QSignalBlocker b1(blurSpin_),       b2(bgThreshSpin_);
    const QSignalBlocker b3(morphKernelSpin_), b4(morphIterSpin_);
    const QSignalBlocker b5(areaMinSpin_),     b6(areaMaxSpin_);
    const QSignalBlocker b7(deformMinSpin_),   b8(deformMaxSpin_);
    blurSpin_->setValue(localConfig_.gaussian_blur_size);
    bgThreshSpin_->setValue(localConfig_.bg_subtract_threshold);
    morphKernelSpin_->setValue(localConfig_.morph_kernel_size);
    morphIterSpin_->setValue(localConfig_.morph_iterations);
    areaMinSpin_->setValue(localConfig_.area_threshold_min);
    areaMaxSpin_->setValue(localConfig_.area_threshold_max);
    deformMinSpin_->setValue(localConfig_.deformability_threshold_min);
    deformMaxSpin_->setValue(localConfig_.deformability_threshold_max);
}

// ---------------------------------------------------------------------------
// matToQImage helper
// ---------------------------------------------------------------------------

QImage BatchMaskDialog::matToQImage(const cv::Mat& gray) const {
    if (gray.empty()) return {};
    cv::Mat rgb;
    cv::cvtColor(gray, rgb, cv::COLOR_GRAY2RGB);
    return QImage(rgb.data, rgb.cols, rgb.rows,
                  static_cast<int>(rgb.step),
                  QImage::Format_RGB888).copy();
}

// ---------------------------------------------------------------------------
// Input loading
// ---------------------------------------------------------------------------

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
    } else if (srcAvi_->isChecked()) {
        const QString path = aviEdit_->text().trimmed();
        if (path.isEmpty()) {
            errorOut = tr("Please pick an AVI file.");
            return false;
        }
        std::vector<std::string> errors;
        if (!backend::services::batch_masks::loadFromAvi(
                path.toStdString(), outGray, outNames, errors)) {
            errorOut = tr("Failed to load AVI file.");
            for (const auto& e : errors) {
                logView_->appendPlainText(QString::fromStdString(e));
            }
            return false;
        }
        if (!errors.empty()) {
            for (const auto& e : errors) {
                logView_->appendPlainText(QString::fromStdString(e));
            }
        }
        if (outGray.empty()) {
            errorOut = tr("No frames decoded from AVI file.");
            return false;
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

// ---------------------------------------------------------------------------
// Running state
// ---------------------------------------------------------------------------

void BatchMaskDialog::setRunning(bool running) {
    runBtn_->setEnabled(!running);
    srcHdf5_->setEnabled(!running && !hdf5LoadedPath_.isEmpty());
    srcFolder_->setEnabled(!running);
    srcAvi_->setEnabled(!running);
    folderBrowseBtn_->setEnabled(!running && srcFolder_->isChecked());
    aviBrowseBtn_->setEnabled(!running && srcAvi_->isChecked());
    prevFrameBtn_->setEnabled(!running && previewFrameIndex_ > 0);
    nextFrameBtn_->setEnabled(!running && previewFrameIndex_ < previewFrameTotal_ - 1);
    setBgBtn_->setEnabled(!running);
    clearBgBtn_->setEnabled(!running);
}

// ---------------------------------------------------------------------------
// Run
// ---------------------------------------------------------------------------

void BatchMaskDialog::onRun() {
    logView_->clear();
    results_.clear();
    savedHdf5Path_.clear();

    const QString outputPath = computeAutoOutputPath();
    if (outputPath.isEmpty()) {
        QMessageBox::warning(this, tr("Cannot determine output path"),
                             tr("Select a source before running."));
        return;
    }

    if (QFile::exists(outputPath)) {
        const auto answer = QMessageBox::question(
            this, tr("Overwrite?"),
            tr("Output file already exists:\n%1\n\nOverwrite?").arg(outputPath),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;
    }

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

    auto& proc        = backend_.processing();
    const auto config = localConfig_;

    const QRect qroi = roiCanvas_->getRoi();
    const backend::services::ProcessingService::Roi roi{
        qroi.x(), qroi.y(), qroi.width(), qroi.height()};
    const cv::Mat background = backgroundMat_;

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

    const bool ok = backend::services::batch_masks::saveMasksToHdf5(
        results_, outputPath.toStdString(), config,
        roi.x, roi.y, roi.w, roi.h, background);

    if (ok) {
        savedHdf5Path_ = outputPath;
        logView_->appendPlainText(tr("Saved: %1").arg(outputPath));
        statusLabel_->setText(tr("Done."));
    } else {
        logView_->appendPlainText(tr("HDF5 write FAILED: %1").arg(outputPath));
        statusLabel_->setText(tr("Done (save failed \u2014 see log)."));
    }

    setRunning(false);
}

QString BatchMaskDialog::computeAutoOutputPath() const {
    if (srcHdf5_->isChecked() && !hdf5LoadedPath_.isEmpty()) {
        const QFileInfo fi(hdf5LoadedPath_);
        return fi.dir().filePath(fi.baseName() + "_remasked.h5");
    }
    if (srcAvi_->isChecked()) {
        const QString path = aviEdit_->text().trimmed();
        if (path.isEmpty()) return {};
        const QFileInfo fi(path);
        return fi.dir().filePath(fi.completeBaseName() + "_remasked.h5");
    }
    const QString folder = folderEdit_->text().trimmed();
    if (folder.isEmpty()) return {};
    const QDir dir(folder);
    const QString name = dir.dirName().isEmpty() ? QStringLiteral("batch") : dir.dirName();
    return dir.filePath(name + "_remasked.h5");
}

} // namespace frontend

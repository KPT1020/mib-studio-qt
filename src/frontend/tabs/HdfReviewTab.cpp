#include "frontend/tabs/HdfReviewTab.h"
#include "ui_HdfReviewTab.h"

#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QMouseEvent>
#include <QPainter>
#include <QFrame>
#include <QSpacerItem>
#include <QFile>
#include <QTextStream>
#include <QFileDialog>
#include <QMessageBox>
#include <QScrollBar>
#include <QEventLoop>
#include <QComboBox>
#include <QChartView>
#include <QLineSeries>
#include <QCoreApplication>
#include <QDir>
#include <map>
#include <QScatterSeries>
#include <QChart>
#include <QValueAxis>
#include <algorithm>
#include <limits>
#ifndef MIB_HAS_QHISTOGRAMSERIES
#if __has_include(<QHistogramSeries>)
#define MIB_HAS_QHISTOGRAMSERIES 1
#else
#define MIB_HAS_QHISTOGRAMSERIES 0
#endif
#endif
#if MIB_HAS_QHISTOGRAMSERIES
#include <QHistogramSeries>
#else
#include <QBarSeries>
#include <QBarSet>
#include <QBarCategoryAxis>
#endif

#include "backend/AppBackend.h"
#include "backend/services/Hdf5Service.h"
#include "backend/services/ProcessingService.h"
#include "frontend/dialogs/BatchMaskDialog.h"
#include "frontend/dialogs/FrameViewerDialog.h"
#include "frontend/models/HdfMetricsModel.h"
#include "frontend/utils/OverlayRenderer.h"

#include <spdlog/spdlog.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace frontend {

class ThumbnailLabel : public QLabel {
    Q_OBJECT
public:
    explicit ThumbnailLabel(int frameIndex, int thumbnailSize, QWidget* parent = nullptr)
        : QLabel(parent), frameIndex_(frameIndex) {
        setAlignment(Qt::AlignCenter);
        setFrameStyle(QFrame::Box);
        setLineWidth(2);
        setStyleSheet("QLabel { border: 2px solid gray; }");
        setMinimumSize(thumbnailSize, thumbnailSize);
        setMaximumSize(thumbnailSize, thumbnailSize);
        setScaledContents(false);
    }

    void setSelected(bool selected) {
        if (selected) {
            setStyleSheet("QLabel { border: 3px solid blue; background-color: lightblue; }");
        } else {
            setStyleSheet("QLabel { border: 2px solid gray; }");
        }
    }

    int frameIndex() const { return frameIndex_; }

signals:
    void clicked(int frameIndex);
    void doubleClicked(int frameIndex);

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            emit clicked(frameIndex_);
        }
        QLabel::mousePressEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            emit doubleClicked(frameIndex_);
        }
        QLabel::mouseDoubleClickEvent(event);
    }

private:
    int frameIndex_;
};

HdfReviewTab::HdfReviewTab(backend::AppBackend& backend, QWidget* parent)
    : QWidget(parent), ui(new Ui::HdfReviewTab), backend_(backend) {
    ui->setupUi(this);

    // Configure thumbnail cache (store up to ~2048 thumbnails)
    thumbnailCache_.setMaxCost(2048);
    SPDLOG_INFO("HdfReviewTab: thumbnail cache size set to {}", 2048);

    // Connect button signals
    connect(ui->selectFileBtn, &QPushButton::clicked, this, &HdfReviewTab::onSelectFile);
    connect(ui->closeFileBtn, &QPushButton::clicked, this, &HdfReviewTab::onCloseFile);
    connect(ui->exportMetricsBtn, &QPushButton::clicked, this, &HdfReviewTab::onExportMetrics);
    connect(ui->exportAllBtn, &QPushButton::clicked, this, &HdfReviewTab::onExportAll);
    connect(ui->exportChartsBtn, &QPushButton::clicked, this, &HdfReviewTab::onExportCharts);
    connect(ui->regenerateMasksBtn, &QPushButton::clicked, this, &HdfReviewTab::onRegenerateMasks);
    connect(ui->overlayModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &HdfReviewTab::onOverlayModeChanged);
    connect(ui->roiOverlayCheck, &QCheckBox::toggled, this, &HdfReviewTab::onToggleRoiOverlay);

    // Setup valid frames tab widgets
    connect(ui->validImageScroll->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &HdfReviewTab::onScrollValueChanged);
    ui->validMetricsTable->horizontalHeader()->setStretchLastSection(true);
    validMetricsModel_ = new HdfMetricsModel(ui->validMetricsTable);
    validMetricsModel_->setPixelToMicronFactor(backend_.processing().getPixelToMicronFactor());
    validMetricsModel_->setSource(&validFrames_);
    ui->validMetricsTable->setModel(validMetricsModel_);

    // Setup invalid frames tab widgets
    connect(ui->invalidImageScroll->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &HdfReviewTab::onScrollValueChanged);
    ui->invalidMetricsTable->horizontalHeader()->setStretchLastSection(true);
    invalidMetricsModel_ = new HdfMetricsModel(ui->invalidMetricsTable);
    invalidMetricsModel_->setPixelToMicronFactor(backend_.processing().getPixelToMicronFactor());
    invalidMetricsModel_->setSource(&invalidFrames_);
    ui->invalidMetricsTable->setModel(invalidMetricsModel_);

    // Add bottom spacers to grids
    validBottomSpacer_ = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Fixed);
    ui->validImageGrid->addItem(validBottomSpacer_, 0, 0, 1, GRID_COLUMNS);
    invalidBottomSpacer_ = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Fixed);
    ui->invalidImageGrid->addItem(invalidBottomSpacer_, 0, 0, 1, GRID_COLUMNS);

    // Charts tab - replace placeholders with actual chart views
    // Left side: Scatter plot chart
    scatterPlotChart_ = new QChart();
    scatterSeries_ = new QScatterSeries();
    scatterSeries_->setMarkerSize(6.0);
    scatterSeries_->setName("Valid Frames");
    scatterPlotChart_->addSeries(scatterSeries_);
    scatterPlotChart_->setTitle("Deformability vs Area (μm²)");
    scatterPlotChart_->legend()->setVisible(false);
    
    scatterXAxis_ = new QValueAxis();
    scatterXAxis_->setTitleText("Area (μm²)");
    scatterYAxis_ = new QValueAxis();
    scatterYAxis_->setTitleText("Deformability");
    scatterPlotChart_->addAxis(scatterXAxis_, Qt::AlignBottom);
    scatterPlotChart_->addAxis(scatterYAxis_, Qt::AlignLeft);
    scatterSeries_->attachAxis(scatterXAxis_);
    scatterSeries_->attachAxis(scatterYAxis_);
    
    // Load isoelastic curves overlay
    loadIsoelasticCurves();
    
    scatterPlotView_ = new QChartView(scatterPlotChart_, ui->chartsTab);
    scatterPlotView_->setRenderHint(QPainter::Antialiasing);
    scatterPlotView_->setMinimumHeight(300);
    // Replace placeholder with actual chart view
    int scatterIndex = ui->chartsLayout->indexOf(ui->scatterPlotViewPlaceholder);
    ui->chartsLayout->removeWidget(ui->scatterPlotViewPlaceholder);
    ui->scatterPlotViewPlaceholder->deleteLater();
    ui->chartsLayout->insertWidget(scatterIndex, scatterPlotView_, 1);
    
    // Right side: Histogram chart
    histogramChart_ = new QChart();
    histogramChart_->setTitle("Ring Width Distribution");
    histogramChart_->legend()->setVisible(false);
    
    histogramYAxis_ = new QValueAxis();
    histogramYAxis_->setTitleText("Frequency");
    histogramChart_->addAxis(histogramYAxis_, Qt::AlignLeft);
    
#if MIB_HAS_QHISTOGRAMSERIES
    histogramSeries_ = new QHistogramSeries();
    histogramSeries_->setName("Ring Width");
    histogramChart_->addSeries(histogramSeries_);
    histogramXAxis_ = new QValueAxis();
    histogramXAxis_->setLabelsAngle(-90);
    histogramXAxis_->setLabelFormat("%.2f");
    histogramChart_->addAxis(histogramXAxis_, Qt::AlignBottom);
    histogramSeries_->attachAxis(histogramXAxis_);
    histogramSeries_->attachAxis(histogramYAxis_);
#else
    histogramBarSeries_ = new QBarSeries();
    histogramChart_->addSeries(histogramBarSeries_);
    histogramCategoryAxis_ = new QBarCategoryAxis();
    histogramCategoryAxis_->setLabelsAngle(-90);
    histogramChart_->addAxis(histogramCategoryAxis_, Qt::AlignBottom);
    histogramBarSeries_->attachAxis(histogramCategoryAxis_);
    histogramBarSeries_->attachAxis(histogramYAxis_);
    histogramXAxis_ = nullptr;
#endif
    
    histogramView_ = new QChartView(histogramChart_, ui->chartsTab);
    histogramView_->setRenderHint(QPainter::Antialiasing);
    histogramView_->setMinimumHeight(300);
    // Replace placeholder with actual chart view
    int histogramIndex = ui->chartsLayout->indexOf(ui->histogramViewPlaceholder);
    ui->chartsLayout->removeWidget(ui->histogramViewPlaceholder);
    ui->histogramViewPlaceholder->deleteLater();
    ui->chartsLayout->insertWidget(histogramIndex, histogramView_, 1);

    // Connect tab and table signals
    connect(ui->frameTypeTabs, QOverload<int>::of(&QTabWidget::currentChanged), 
            this, &HdfReviewTab::onTabChanged);
    connect(ui->validMetricsTable->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &HdfReviewTab::onTableSelectionChanged);
    connect(ui->invalidMetricsTable->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &HdfReviewTab::onTableSelectionChanged);
    connect(ui->validMetricsTable, &QTableView::doubleClicked,
            this, [this](const QModelIndex& idx) {
                if (idx.isValid()) onViewFrameDetails(idx.row());
            });
    connect(ui->invalidMetricsTable, &QTableView::doubleClicked,
            this, [this](const QModelIndex& idx) {
                if (idx.isValid()) onViewFrameDetails(idx.row());
            });
}

HdfReviewTab::~HdfReviewTab() {
    // Clean up isoelastic curve line series
    for (auto it = isoelasticCurves_.begin(); it != isoelasticCurves_.end(); ++it) {
        QLineSeries* series = *it;
        if (series) {
            delete series;
        }
    }
    isoelasticCurves_.clear();
    delete ui;
}

void HdfReviewTab::onSelectFile() {
    QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("Open HDF File"),
        "",
        tr("HDF5 Files (*.h5 *.hdf5);;All Files (*)")
    );

    if (!filePath.isEmpty()) {
        loadHdfFile(filePath);
    }
}

void HdfReviewTab::onCloseFile() {
    clearDisplay();
    hdfReader_.reset();
    ui->filePathLabel->setText(tr("No file selected"));
    ui->statusLabel->setText(tr("Ready"));
    ui->closeFileBtn->setEnabled(false);
    ui->overlayModeLabel->setEnabled(false);
    ui->overlayModeCombo->setEnabled(false);
    ui->overlayModeCombo->setCurrentIndex(0);
    overlayMode_ = OverlayMode::None;
    SPDLOG_INFO("HdfReviewTab: file closed by user");
}

void HdfReviewTab::loadHdfFile(const QString& filePath) {
    ui->statusLabel->setText(tr("Loading..."));
    ui->filePathLabel->setText(filePath);
    clearDisplay();

    SPDLOG_INFO("HdfReviewTab: opening file '{}'", filePath.toStdString());
    // Open and retain HDF5 file for the lifetime of this review session
    hdfReader_.reset();
    hdfReader_ = std::make_unique<backend::services::Hdf5Service>();
    if (!hdfReader_->loadFile(filePath.toStdString())) {
        const bool exists = QFile::exists(filePath);
        const QString detail = exists
            ? tr("The file exists but its HDF5 metadata is corrupt, likely caused by "
                 "an interrupted write (crash or forced close during an experiment).\n\n"
                 "Frame data may be partially recoverable using the h5recover tool "
                 "from the HDF5 utilities package.")
            : tr("File not found.");
        QMessageBox::critical(this, tr("Cannot Open HDF5 File"),
                              tr("Failed to open:\n%1\n\n%2").arg(filePath).arg(detail));
        ui->statusLabel->setText(tr("Error loading file"));
        return;
    }

    // Read experiment info and ROI
    uint64_t startTimeNs = 0, endTimeNs = 0;
    size_t totalValid = 0, totalInvalid = 0;
    backend::services::ProcessingService::Roi loadedRoi{0, 0, 0, 0};
    if (hdfReader_->readExperimentInfo(startTimeNs, endTimeNs, totalValid, totalInvalid, &loadedRoi)) {
        ui->statusLabel->setText(QString("Valid: %1, Invalid: %2")
                             .arg(totalValid).arg(totalInvalid));
        roi_ = loadedRoi;
        SPDLOG_INFO("Loaded ROI from HDF5: x={}, y={}, w={}, h={}", roi_.x, roi_.y, roi_.w, roi_.h);
        // Enable overlay checkbox if we have frames (for processing overlay) or valid ROI
        ui->roiOverlayCheck->setEnabled(true);
    } else {
        roi_ = {0, 0, 0, 0};
        SPDLOG_WARN("Failed to read experiment info or ROI not found in HDF5 file");
        // Still enable overlay checkbox if we have frames (for processing overlay)
        ui->roiOverlayCheck->setEnabled(false);
    }

    // Log datasets info for debugging
    size_t count = 0; int h = 0, w = 0, c = 0;
    size_t validImagesCount = 0;
    size_t invalidImagesCount = 0;
    if (hdfReader_->getDatasetInfo("/valid_frames/images", count, h, w, c)) {
        SPDLOG_INFO("Dataset /valid_frames/images: count={}, H={}, W={}, C={}", count, h, w, c);
        validImagesCount = count;
    }
    if (hdfReader_->getDatasetInfo("/valid_frames/masks", count, h, w, c)) {
        SPDLOG_INFO("Dataset /valid_frames/masks:  count={}, H={}, W={}, C={}", count, h, w, c);
    }
    if (hdfReader_->getDatasetInfo("/invalid_frames/images", count, h, w, c)) {
        SPDLOG_INFO("Dataset /invalid_frames/images: count={}, H={}, W={}, C={}", count, h, w, c);
        invalidImagesCount = count;
    }
    if (hdfReader_->getDatasetInfo("/invalid_frames/masks", count, h, w, c)) {
        SPDLOG_INFO("Dataset /invalid_frames/masks:  count={}, H={}, W={}, C={}", count, h, w, c);
    }

    // Read metadata only (images/masks will be fetched on-demand)
    if (!hdfReader_->readValidMetadata(validFrames_)) {
        SPDLOG_WARN("Failed to read valid metadata or none found");
        validFrames_.clear();
    }

    if (!hdfReader_->readInvalidMetadata(invalidFrames_)) {
        SPDLOG_WARN("Failed to read invalid metadata or none found");
        invalidFrames_.clear();
    }

    // Keep file open in hdfReader_ for subsequent on-demand reads (thumbnails/viewer)

    // Populate UI
    updateImageGrid(validFrames_);
    updateMetricsTable(validFrames_);
    updateImageGrid(invalidFrames_);
    updateMetricsTable(invalidFrames_);

    // Enable export buttons if we have any data
    bool hasData = !validFrames_.empty() || !invalidFrames_.empty();
    ui->exportMetricsBtn->setEnabled(hasData);
    ui->exportAllBtn->setEnabled(hasData);
    ui->exportChartsBtn->setEnabled(hasData);
    ui->closeFileBtn->setEnabled(hasData);
    
    // Enable overlay controls if we have frames
    if (hasData) {
        ui->overlayModeLabel->setEnabled(true);
        ui->overlayModeCombo->setEnabled(true);
        ui->roiOverlayCheck->setEnabled(true);
    }

    // Update charts tab with snapshots from HDF5
    updateCharts();

    // Prefer actual dataset/metadata counts for status display (experiment info may be stale)
    {
        const size_t shownValid = !validFrames_.empty() ? validFrames_.size()
                                 : (validImagesCount > 0 ? validImagesCount : totalValid);
        const size_t shownInvalid = !invalidFrames_.empty() ? invalidFrames_.size()
                                   : (invalidImagesCount > 0 ? invalidImagesCount : totalInvalid);
        ui->statusLabel->setText(QString("Valid: %1, Invalid: %2")
                              .arg(static_cast<qulonglong>(shownValid))
                              .arg(static_cast<qulonglong>(shownInvalid)));
    }

    SPDLOG_INFO("Loaded HDF file: {} valid frames, {} invalid frames", 
               validFrames_.size(), invalidFrames_.size());
}

void HdfReviewTab::populateFrames(const std::vector<backend::services::ProcessedFrame>& frames, bool isValid) {
    // This method is kept for potential future use but currently not needed
    // as frames are stored directly in loadHdfFile
    if (isValid) {
        validFrames_ = frames;
        updateImageGrid(validFrames_);
        updateMetricsTable(validFrames_);
    } else {
        invalidFrames_ = frames;
        updateImageGrid(invalidFrames_);
        updateMetricsTable(invalidFrames_);
    }
}

void HdfReviewTab::clearDisplay() {
    validFrames_.clear();
    invalidFrames_.clear();
    selectedFrameIndex_ = -1;
    validThumbnailsLoaded_ = 0;
    invalidThumbnailsLoaded_ = 0;
    roi_ = {0, 0, 0, 0};
    showRoiOverlay_ = false;
    thumbnailCache_.clear();
    validScrollValue_ = 0;
    invalidScrollValue_ = 0;
    
    // Disable export buttons and ROI overlay when no data
    ui->exportMetricsBtn->setEnabled(false);
    ui->exportAllBtn->setEnabled(false);
    ui->exportChartsBtn->setEnabled(false);
    ui->roiOverlayCheck->setEnabled(false);
    ui->roiOverlayCheck->setChecked(false);

    // Clear valid frames grid
    QLayoutItem* item;
    while ((item = ui->validImageGrid->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
    // After clearing, the spacer pointer may be dangling; reset it
    validBottomSpacer_ = nullptr;
    validTopSpacer_ = nullptr;

    // Clear invalid frames grid
    while ((item = ui->invalidImageGrid->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
    // After clearing, the spacer pointer may be dangling; reset it
    invalidBottomSpacer_ = nullptr;
    invalidTopSpacer_ = nullptr;

    if (validMetricsModel_) validMetricsModel_->setSource(&validFrames_);
    if (invalidMetricsModel_) invalidMetricsModel_->setSource(&invalidFrames_);

    // Clear charts
    if (scatterSeries_) {
        scatterSeries_->clear();
    }
    // Clear isoelastic curves (they will be reloaded when charts are regenerated)
    for (auto it = isoelasticCurves_.begin(); it != isoelasticCurves_.end(); ++it) {
        QLineSeries* series = *it;
        if (series) {
            scatterPlotChart_->removeSeries(series);
            delete series;
        }
    }
    isoelasticCurves_.clear();
    if (scatterXAxis_ && scatterYAxis_) {
        scatterXAxis_->setRange(0, 1000);
        scatterYAxis_->setRange(0, 1);
    }
#if MIB_HAS_QHISTOGRAMSERIES
    if (histogramSeries_) {
        histogramSeries_->clear();
    }
#else
    if (histogramBarSeries_) {
        histogramBarSeries_->clear();
    }
#endif
    if (histogramYAxis_) {
        histogramYAxis_->setRange(0, 1);
    }
}

void HdfReviewTab::updateImageGrid(const std::vector<backend::services::ProcessedFrame>& frames) {
    // Determine which grid to use based on which frames vector we're updating
    bool isValid = (&frames == &validFrames_);
    QGridLayout* grid = isValid ? ui->validImageGrid : ui->invalidImageGrid;
    SPDLOG_DEBUG("HdfReviewTab: updateImageGrid {} frames={}",
                 isValid ? "valid" : "invalid", frames.size());
    
    // Clear existing thumbnails
    QLayoutItem* item;
    while ((item = grid->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
    // Reset spacer pointer for this grid since all items were removed
    if (isValid) {
        validBottomSpacer_ = nullptr;
    } else {
        invalidBottomSpacer_ = nullptr;
    }

    // Reset loaded count
    if (isValid) {
        validThumbnailsLoaded_ = 0;
    } else {
        invalidThumbnailsLoaded_ = 0;
    }

    // Only load initial batch of thumbnails to avoid memory issues
    size_t initialCount = std::min(frames.size(), INITIAL_THUMBNAIL_COUNT);
    loadThumbnailsBatch(frames, 0, initialCount, isValid);
    
    // Update loaded count
    if (isValid) {
        validThumbnailsLoaded_ = initialCount;
    } else {
        invalidThumbnailsLoaded_ = initialCount;
    }

    // Virtualize remaining space: adjust bottom spacer height instead of creating thousands of placeholders
    const size_t totalRows = (frames.size() + GRID_COLUMNS - 1) / GRID_COLUMNS;
    const size_t loadedRows = (initialCount + GRID_COLUMNS - 1) / GRID_COLUMNS;
    const int cellH = THUMBNAIL_SIZE + 8; // approximate spacing/margins
    const int remainingRows = static_cast<int>(totalRows > loadedRows ? (totalRows - loadedRows) : 0);
    const int spacerH = remainingRows * cellH;
    if (isValid) {
        if (validBottomSpacer_) {
            ui->validImageGrid->removeItem(validBottomSpacer_);
            delete validBottomSpacer_;
        }
        validBottomSpacer_ = new QSpacerItem(0, spacerH, QSizePolicy::Minimum, QSizePolicy::Fixed);
        ui->validImageGrid->addItem(validBottomSpacer_, static_cast<int>(loadedRows), 0, 1, GRID_COLUMNS);
    } else {
        if (invalidBottomSpacer_) {
            ui->invalidImageGrid->removeItem(invalidBottomSpacer_);
            delete invalidBottomSpacer_;
        }
        invalidBottomSpacer_ = new QSpacerItem(0, spacerH, QSizePolicy::Minimum, QSizePolicy::Fixed);
        ui->invalidImageGrid->addItem(invalidBottomSpacer_, static_cast<int>(loadedRows), 0, 1, GRID_COLUMNS);
    }
}

void HdfReviewTab::loadThumbnailsBatch(const std::vector<backend::services::ProcessedFrame>& frames,
                                        size_t startIndex, size_t count, bool isValid) {
    QGridLayout* grid = isValid ? ui->validImageGrid : ui->invalidImageGrid;
    size_t endIndex = std::min(startIndex + count, frames.size());
    SPDLOG_DEBUG("HdfReviewTab: loadThumbnailsBatch {} start={} count={} end={}",
                 isValid ? "valid" : "invalid", startIndex, count, endIndex);
#ifdef _WIN32
    {
        MEMORYSTATUSEX st;
        st.dwLength = sizeof(st);
        if (GlobalMemoryStatusEx(&st)) {
            SPDLOG_INFO("Mem before batch: load={}%, avail_phys_MB={}, avail_page_MB={}",
                        st.dwMemoryLoad,
                        static_cast<unsigned long long>(st.ullAvailPhys / (1024 * 1024)),
                        static_cast<unsigned long long>(st.ullAvailPageFile / (1024 * 1024)));
        }
    }
#endif
    
    for (size_t i = startIndex; i < endIndex; ++i) {
        // Cache key: [valid_flag (1 bit)] [reserved (15 bits)] [index (48 bits)]
        const qulonglong key = (static_cast<qulonglong>(isValid ? 1 : 0) << 63)
                             | (static_cast<qulonglong>(i) & 0x0000FFFFFFFFFFFFull);

        QImage* cached = thumbnailCache_.object(key);
        QImage thumbImage;
        if (cached) {
            thumbImage = *cached;
        } else {
            // Dataset paths
            const std::string imgPath = isValid ? "/valid_frames/images" : "/invalid_frames/images";
            const std::string maskPath = isValid ? "/valid_frames/masks"  : "/invalid_frames/masks";

            // Read original image by dataset position (i), not by frame.index
            cv::Mat original;
            if (!hdfReader_ || !hdfReader_->readImageByIndex(imgPath, i, original)) {
                SPDLOG_WARN("HdfReviewTab: failed to read original image {}[{}]", imgPath, i);
                // If image cannot be read, leave placeholder (already added)
                continue;
            }

            // Optional processing overlay when overlay mode is not None
            const auto& framesRef = isValid ? validFrames_ : invalidFrames_;
            const backend::services::FilterResult* validation = (i < framesRef.size()) ? &framesRef[i].validation : nullptr;
            if (overlayMode_ != OverlayMode::None) {
                cv::Mat mask;
                if (hdfReader_->readImageByIndex(maskPath, i, mask) && !mask.empty()) {
                    thumbImage = createProcessingOverlay(original, mask, validation, overlayMode_);
                } else {
                    SPDLOG_DEBUG("HdfReviewTab: mask not available for {}[{}] (overlay on)", maskPath, i);
                    thumbImage = matToQImage(original);
                }
            } else {
                thumbImage = matToQImage(original);
            }

            // ROI rectangle overlay if enabled
            if (showRoiOverlay_ && !thumbImage.isNull() && roi_.w > 0 && roi_.h > 0) {
                thumbImage = drawRoiOverlay(thumbImage, original.cols, original.rows);
            }

            // Scale and cache
            if (!thumbImage.isNull()) {
                QImage scaled = thumbImage.scaled(THUMBNAIL_SIZE, THUMBNAIL_SIZE, 
                                                  Qt::KeepAspectRatio, Qt::SmoothTransformation);
                auto* stored = new QImage(scaled);
                thumbnailCache_.insert(key, stored, 1);
                thumbImage = scaled;
                SPDLOG_TRACE("HdfReviewTab: cached thumbnail key={} ({}), size={}x{}",
                             key, isValid ? "valid" : "invalid",
                             scaled.width(), scaled.height());
            }
        }
        
        // Scale to thumbnail size
        QImage scaled = thumbImage; // already scaled if newly created; if from cache, should be scaled too
        
        auto* label = new ThumbnailLabel(static_cast<int>(i), THUMBNAIL_SIZE, grid->parentWidget());
        label->setPixmap(QPixmap::fromImage(scaled));
        label->setToolTip(QString("Frame %1\nDouble-click to view details").arg(i));
        
        connect(label, &ThumbnailLabel::clicked, this, &HdfReviewTab::onThumbnailClicked);
        connect(label, &ThumbnailLabel::doubleClicked, this, &HdfReviewTab::onThumbnailDoubleClicked);
        
        int row = static_cast<int>(i) / GRID_COLUMNS;
        int col = static_cast<int>(i) % GRID_COLUMNS;
        
        // Remove placeholder if exists
        QLayoutItem* existingItem = grid->itemAtPosition(row, col);
        if (existingItem && existingItem->widget()) {
            QWidget* existingWidget = existingItem->widget();
            if (qobject_cast<QLabel*>(existingWidget) && 
                !qobject_cast<ThumbnailLabel*>(existingWidget)) {
                grid->removeWidget(existingWidget);
                existingWidget->deleteLater();
            }
        }
        
        grid->addWidget(label, row, col);

        SPDLOG_DEBUG("HdfReviewTab: loaded thumbnail {} ({})", i, isValid ? "valid" : "invalid");
    }

    // Adjust spacer height to reflect newly loaded rows
    const size_t totalRows = (frames.size() + GRID_COLUMNS - 1) / GRID_COLUMNS;
    const size_t loadedRows = (endIndex + GRID_COLUMNS - 1) / GRID_COLUMNS;
    const int cellH = THUMBNAIL_SIZE + 8;
    const int remainingRows = static_cast<int>(totalRows > loadedRows ? (totalRows - loadedRows) : 0);
    const int spacerH = remainingRows * cellH;
    if (isValid) {
        if (validBottomSpacer_) {
            ui->validImageGrid->removeItem(validBottomSpacer_);
            delete validBottomSpacer_;
        }
        validBottomSpacer_ = new QSpacerItem(0, spacerH, QSizePolicy::Minimum, QSizePolicy::Fixed);
        ui->validImageGrid->addItem(validBottomSpacer_, static_cast<int>(loadedRows), 0, 1, GRID_COLUMNS);
    } else {
        if (invalidBottomSpacer_) {
            ui->invalidImageGrid->removeItem(invalidBottomSpacer_);
            delete invalidBottomSpacer_;
        }
        invalidBottomSpacer_ = new QSpacerItem(0, spacerH, QSizePolicy::Minimum, QSizePolicy::Fixed);
        ui->invalidImageGrid->addItem(invalidBottomSpacer_, static_cast<int>(loadedRows), 0, 1, GRID_COLUMNS);
    }
#ifdef _WIN32
    {
        MEMORYSTATUSEX st;
        st.dwLength = sizeof(st);
        if (GlobalMemoryStatusEx(&st)) {
            SPDLOG_INFO("Mem after batch: load={}%, avail_phys_MB={}, avail_page_MB={}",
                        st.dwMemoryLoad,
                        static_cast<unsigned long long>(st.ullAvailPhys / (1024 * 1024)),
                        static_cast<unsigned long long>(st.ullAvailPageFile / (1024 * 1024)));
        }
    }
#endif
}

void HdfReviewTab::onScrollValueChanged(int value) {
    QScrollArea* scrollArea = isShowingValid_ ? ui->validImageScroll : ui->invalidImageScroll;
    const auto& frames = isShowingValid_ ? validFrames_ : invalidFrames_;
    size_t& loadedCount = isShowingValid_ ? validThumbnailsLoaded_ : invalidThumbnailsLoaded_;
    
    if (frames.empty() || loadedCount >= frames.size()) {
        // Still ensure visible items reflect current overlay state
        refreshVisibleThumbnails(isShowingValid_);
        pruneOffscreenThumbnails(isShowingValid_);
        return;
    }
    
    // Trigger loading when near the bottom of the CURRENT content (post-pruning).
    // Using scrollbar maximum ensures we don't depend on internal loaded counters.
    QScrollBar* scrollBar = scrollArea->verticalScrollBar();
    const int cellH = THUMBNAIL_SIZE + 8; // keep in sync with grid estimation
    int threshold = std::max(0, scrollBar->maximum() - (cellH * 2));

    if (value >= threshold && loadedCount < frames.size()) {
        // Load next batch
        size_t batchSize = std::min(BATCH_THUMBNAIL_COUNT, frames.size() - loadedCount);
        loadThumbnailsBatch(frames, loadedCount, batchSize, isShowingValid_);
        loadedCount += batchSize;
        
        SPDLOG_DEBUG("Loaded thumbnail batch: {} total loaded out of {}", loadedCount, frames.size());
    }

    // Always refresh visible thumbnails (ensures overlay changes apply lazily)
    refreshVisibleThumbnails(isShowingValid_);
    pruneOffscreenThumbnails(isShowingValid_);
}

void HdfReviewTab::updateMetricsTable(const std::vector<backend::services::ProcessedFrame>& frames) {
    // Determine which model to use based on which frames vector we're updating
    bool isValid = (&frames == &validFrames_);
    if (isValid) {
        if (validMetricsModel_) {
            validMetricsModel_->setSource(&validFrames_);
            ui->validMetricsTable->resizeColumnsToContents();
        }
    } else {
        if (invalidMetricsModel_) {
            invalidMetricsModel_->setSource(&invalidFrames_);
            ui->invalidMetricsTable->resizeColumnsToContents();
        }
    }
}

QImage HdfReviewTab::matToQImage(const cv::Mat& mat) const {
    if (mat.empty()) {
        return QImage();
    }

    if (mat.type() == CV_8UC1) {
        // Grayscale
        QImage img(mat.data, mat.cols, mat.rows, static_cast<int>(mat.step), QImage::Format_Grayscale8);
        return img.copy();
    } else if (mat.type() == CV_8UC3) {
        // BGR to RGB
        cv::Mat rgb;
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
        QImage img(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888);
        return img.copy();
    } else if (mat.type() == CV_8UC4) {
        // BGRA to RGBA
        cv::Mat rgba;
        cv::cvtColor(mat, rgba, cv::COLOR_BGRA2RGBA);
        QImage img(rgba.data, rgba.cols, rgba.rows, static_cast<int>(rgba.step), QImage::Format_RGBA8888);
        return img.copy();
    }

    // Fallback: convert to grayscale
    cv::Mat gray;
    cv::cvtColor(mat, gray, cv::COLOR_BGR2GRAY);
    QImage img(gray.data, gray.cols, gray.rows, static_cast<int>(gray.step), QImage::Format_Grayscale8);
    return img.copy();
}

void HdfReviewTab::onTabChanged(int index) {
    // Save previous tab's scroll position
    {
        QScrollArea* prevScroll = isShowingValid_ ? ui->validImageScroll : ui->invalidImageScroll;
        if (prevScroll && prevScroll->verticalScrollBar()) {
            int prevVal = prevScroll->verticalScrollBar()->value();
            if (isShowingValid_) {
                validScrollValue_ = prevVal;
            } else {
                invalidScrollValue_ = prevVal;
            }
        }
    }

    isShowingValid_ = (index == 0);

    // Do not rebuild image grids on tab switch; just refresh metrics view
    if (isShowingValid_) {
        updateMetricsTable(validFrames_);
    } else {
        updateMetricsTable(invalidFrames_);
    }

    // Restore saved scroll position for the new tab
    {
        QScrollArea* currScroll = isShowingValid_ ? ui->validImageScroll : ui->invalidImageScroll;
        if (currScroll && currScroll->verticalScrollBar()) {
            int targetVal = isShowingValid_ ? validScrollValue_ : invalidScrollValue_;
            currScroll->verticalScrollBar()->setValue(targetVal);
        }
    }
}

void HdfReviewTab::onThumbnailClicked(int frameIndex) {
    setSelectedFrame(frameIndex);
}

void HdfReviewTab::onThumbnailDoubleClicked(int frameIndex) {
    showFrameViewer(frameIndex);
}

void HdfReviewTab::onViewFrameDetails(int frameIndex) {
    showFrameViewer(frameIndex);
}

void HdfReviewTab::onRegenerateMasks() {
    // Grab the currently loaded HDF5 path (if any) so the dialog can offer
    // "Current HDF5 frames" as a source.
    QString loadedPath;
    if (hdfReader_) {
        const QString label = ui->filePathLabel->text();
        if (label != tr("No file selected")) loadedPath = label;
    }

    BatchMaskDialog dlg(backend_, loadedPath, this);
    if (dlg.exec() != QDialog::Accepted) return;

    if (!dlg.displayRequested()) return;

    // Replace the current in-memory frame set with the batch result so the
    // thumbnail grid refreshes against the newly computed masks.
    const auto& out = dlg.processedFrames();
    if (out.empty()) return;

    std::vector<backend::services::ProcessedFrame> valid, invalid;
    valid.reserve(out.size());
    invalid.reserve(out.size());
    for (const auto& f : out) {
        if (f.validation.isValid) valid.push_back(f);
        else invalid.push_back(f);
    }

    // Reset caches tied to the old dataset indexing.
    thumbnailCache_.clear();
    validThumbnailsLoaded_ = 0;
    invalidThumbnailsLoaded_ = 0;

    validFrames_ = std::move(valid);
    invalidFrames_ = std::move(invalid);

    populateFrames(validFrames_, true);
    populateFrames(invalidFrames_, false);
    updateCharts();

    ui->statusLabel->setText(
        tr("Regenerated masks: %1 valid, %2 invalid")
            .arg(validFrames_.size()).arg(invalidFrames_.size()));
    SPDLOG_INFO("HdfReviewTab: regenerated masks ({} valid, {} invalid)",
                validFrames_.size(), invalidFrames_.size());
}

void HdfReviewTab::onTableSelectionChanged() {
    QTableView* table = isShowingValid_ ? ui->validMetricsTable : ui->invalidMetricsTable;
    if (!table || !table->selectionModel()) return;
    const QModelIndexList rows = table->selectionModel()->selectedRows();
    if (!rows.isEmpty()) {
        setSelectedFrame(rows.first().row());
    }
}

void HdfReviewTab::setSelectedFrame(int frameIndex) {
    if (frameIndex < 0) {
        selectedFrameIndex_ = -1;
        return;
    }

    const auto& frames = isShowingValid_ ? validFrames_ : invalidFrames_;
    if (frameIndex >= static_cast<int>(frames.size())) {
        return;
    }

    selectedFrameIndex_ = frameIndex;

    // Update thumbnail selection
    QGridLayout* grid = isShowingValid_ ? ui->validImageGrid : ui->invalidImageGrid;
    for (int i = 0; i < grid->count(); ++i) {
        QLayoutItem* item = grid->itemAt(i);
        if (item && item->widget()) {
            auto* label = qobject_cast<ThumbnailLabel*>(item->widget());
            if (label) {
                label->setSelected(label->frameIndex() == frameIndex);
            }
        }
    }

    // Update table selection
    QTableView* table = isShowingValid_ ? ui->validMetricsTable : ui->invalidMetricsTable;
    if (table && table->model()) {
        QModelIndex idx = table->model()->index(frameIndex, 0);
        if (idx.isValid() && table->selectionModel()) {
            table->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            table->scrollTo(idx);
        }
    }
}

void HdfReviewTab::onExportMetrics() {
    if (validFrames_.empty() && invalidFrames_.empty()) {
        QMessageBox::information(this, tr("Export Metrics"),
                                 tr("No metrics data available to export."));
        return;
    }

    QString filePath = QFileDialog::getSaveFileName(
        this,
        tr("Export Metrics to CSV"),
        "",
        tr("CSV Files (*.csv);;All Files (*)")
    );

    if (!filePath.isEmpty()) {
        exportMetricsToCsv(filePath);
    }
}

void HdfReviewTab::exportMetricsToCsv(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Export Error"),
                             tr("Failed to open file for writing:\n%1").arg(filePath));
        return;
    }

    QTextStream out(&file);
    
    // Get conversion factor from backend (pixels to microns)
    const double conversionFactor = backend_.processing().getPixelToMicronFactor();
    // Area conversion: pixels² to microns² = pixels² * (microns/pixel)²
    const double areaConversionFactor = conversionFactor * conversionFactor;
    
    // CSV header
    out << "Frame Type,Index,Timestamp,Deformability,Area,Area (um²),Area Ratio,Ring Ratio,"
        << "Valid,Touches Border,Single Inner,In Range,Inner Count,"
        << "Bright Q1,Bright Q2,Bright Q3,Bright Q4\n";

    // Export valid frames
    for (const auto& frame : validFrames_) {
        const auto& val = frame.validation;
        // Convert area from pixels² to microns²
        double areaMicrons = val.area * areaConversionFactor;
        out << "Valid,";
        out << frame.index << ",";
        out << frame.timestampNs << ",";
        out << QString::number(val.deformability, 'f', 3) << ",";
        out << QString::number(val.area, 'f', 2) << ",";
        out << QString::number(areaMicrons, 'f', 2) << ",";
        out << QString::number(val.areaRatio, 'f', 3) << ",";
        out << QString::number(val.ringRatio, 'f', 3) << ",";
        out << (val.isValid ? "Yes" : "No") << ",";
        out << (val.touchesBorder ? "Yes" : "No") << ",";
        out << (val.hasSingleInnerContour ? "Yes" : "No") << ",";
        out << (val.inRange ? "Yes" : "No") << ",";
        out << val.innerContourCount << ",";
        out << QString::number(val.brightness.q1, 'f', 2) << ",";
        out << QString::number(val.brightness.q2, 'f', 2) << ",";
        out << QString::number(val.brightness.q3, 'f', 2) << ",";
        out << QString::number(val.brightness.q4, 'f', 2) << "\n";
    }

    // Export invalid frames
    for (const auto& frame : invalidFrames_) {
        const auto& val = frame.validation;
        // Convert area from pixels² to microns²
        double areaMicrons = val.area * areaConversionFactor;
        out << "Invalid,";
        out << frame.index << ",";
        out << frame.timestampNs << ",";
        out << QString::number(val.deformability, 'f', 3) << ",";
        out << QString::number(val.area, 'f', 2) << ",";
        out << QString::number(areaMicrons, 'f', 2) << ",";
        out << QString::number(val.areaRatio, 'f', 3) << ",";
        out << QString::number(val.ringRatio, 'f', 3) << ",";
        out << (val.isValid ? "Yes" : "No") << ",";
        out << (val.touchesBorder ? "Yes" : "No") << ",";
        out << (val.hasSingleInnerContour ? "Yes" : "No") << ",";
        out << (val.inRange ? "Yes" : "No") << ",";
        out << val.innerContourCount << ",";
        out << QString::number(val.brightness.q1, 'f', 2) << ",";
        out << QString::number(val.brightness.q2, 'f', 2) << ",";
        out << QString::number(val.brightness.q3, 'f', 2) << ",";
        out << QString::number(val.brightness.q4, 'f', 2) << "\n";
    }

    file.close();

    size_t totalFrames = validFrames_.size() + invalidFrames_.size();
    QMessageBox::information(this, tr("Export Complete"),
                           tr("Exported %1 frames (Valid: %2, Invalid: %3) to:\n%4")
                           .arg(totalFrames)
                           .arg(validFrames_.size())
                           .arg(invalidFrames_.size())
                           .arg(filePath));
    
    SPDLOG_INFO("Exported {} frames to CSV: {}", totalFrames, filePath.toStdString());
}

void HdfReviewTab::onOverlayModeChanged(int index) {
    overlayMode_ = static_cast<OverlayMode>(index);
    SPDLOG_INFO("Overlay mode changed to index {} (OverlayMode={})", index, static_cast<int>(overlayMode_));
    thumbnailCache_.clear();

    if (ui->validImageScroll && ui->validImageScroll->verticalScrollBar()) {
        validScrollValue_ = ui->validImageScroll->verticalScrollBar()->value();
    }
    if (ui->invalidImageScroll && ui->invalidImageScroll->verticalScrollBar()) {
        invalidScrollValue_ = ui->invalidImageScroll->verticalScrollBar()->value();
    }
    refreshVisibleThumbnails(true);
    refreshVisibleThumbnails(false);
    pruneOffscreenThumbnails(true);
    pruneOffscreenThumbnails(false);
    if (ui->validImageScroll && ui->validImageScroll->verticalScrollBar()) {
        ui->validImageScroll->verticalScrollBar()->setValue(validScrollValue_);
    }
    if (ui->invalidImageScroll && ui->invalidImageScroll->verticalScrollBar()) {
        ui->invalidImageScroll->verticalScrollBar()->setValue(invalidScrollValue_);
    }
}

void HdfReviewTab::onToggleRoiOverlay(bool enabled) {
    showRoiOverlay_ = enabled;
    SPDLOG_INFO("ROI overlay toggled: {}, ROI: x={}, y={}, w={}, h={}", 
                enabled, roi_.x, roi_.y, roi_.w, roi_.h);
    thumbnailCache_.clear();

    // Preserve current scroll positions
    if (ui->validImageScroll && ui->validImageScroll->verticalScrollBar()) {
        validScrollValue_ = ui->validImageScroll->verticalScrollBar()->value();
    }
    if (ui->invalidImageScroll && ui->invalidImageScroll->verticalScrollBar()) {
        invalidScrollValue_ = ui->invalidImageScroll->verticalScrollBar()->value();
    }

    // Refresh only what is visible in each tab (carousel-like behavior)
    refreshVisibleThumbnails(true);
    refreshVisibleThumbnails(false);
    pruneOffscreenThumbnails(true);
    pruneOffscreenThumbnails(false);

    // Restore scroll positions
    if (ui->validImageScroll && ui->validImageScroll->verticalScrollBar()) {
        ui->validImageScroll->verticalScrollBar()->setValue(validScrollValue_);
    }
    if (ui->invalidImageScroll && ui->invalidImageScroll->verticalScrollBar()) {
        ui->invalidImageScroll->verticalScrollBar()->setValue(invalidScrollValue_);
    }
}

QImage HdfReviewTab::drawRoiOverlay(const QImage& image, int imgWidth, int imgHeight) const {
    if (image.isNull() || roi_.w <= 0 || roi_.h <= 0) {
        return image;
    }

    // Create a copy to draw on
    QImage overlayImage = image.copy();
    QPainter painter(&overlayImage);
    painter.setRenderHint(QPainter::Antialiasing);

    // Calculate ROI rectangle in image coordinates
    // ROI is in original image coordinates, need to scale to current image size
    double scaleX = static_cast<double>(image.width()) / static_cast<double>(imgWidth);
    double scaleY = static_cast<double>(image.height()) / static_cast<double>(imgHeight);
    
    int roiX = static_cast<int>(roi_.x * scaleX);
    int roiY = static_cast<int>(roi_.y * scaleY);
    int roiW = static_cast<int>(roi_.w * scaleX);
    int roiH = static_cast<int>(roi_.h * scaleY);

    // Clamp ROI to image bounds
    roiX = std::max(0, std::min(roiX, image.width() - 1));
    roiY = std::max(0, std::min(roiY, image.height() - 1));
    roiW = std::max(1, std::min(roiW, image.width() - roiX));
    roiH = std::max(1, std::min(roiH, image.height() - roiY));

    // Draw rectangle with red border (thicker for visibility)
    QPen pen(QColor(255, 0, 0), 3); // Red, 3px width for better visibility
    painter.setPen(pen);
    painter.drawRect(roiX, roiY, roiW, roiH);

    return overlayImage;
}

void HdfReviewTab::computeVisibleRange(bool isValid, size_t &outStartIndex, size_t &outEndIndex) const {
    const auto& frames = isValid ? validFrames_ : invalidFrames_;
    outStartIndex = 0;
    outEndIndex = 0;
    if (frames.empty()) return;

    const QScrollArea* scrollArea = isValid ? ui->validImageScroll : ui->invalidImageScroll;
    if (!scrollArea || !scrollArea->verticalScrollBar()) return;

    const int cellH = THUMBNAIL_SIZE + 8;
    const int value = scrollArea->verticalScrollBar()->value();
    const int viewportH = scrollArea->viewport()->height();

    int startRow = value / cellH;
    startRow = std::max(0, startRow - 1); // buffer one row above
    int rowsVisible = (viewportH + cellH - 1) / cellH + 2; // buffer two rows
    int endRow = startRow + rowsVisible;

    const size_t totalRows = (frames.size() + GRID_COLUMNS - 1) / GRID_COLUMNS;
    endRow = std::min<int>(endRow, static_cast<int>(totalRows));

    outStartIndex = static_cast<size_t>(startRow) * GRID_COLUMNS;
    outEndIndex = std::min(frames.size(), static_cast<size_t>(endRow) * GRID_COLUMNS);
}

QImage HdfReviewTab::buildThumbnailForIndex(size_t index, bool isValid) {
    // Cache key: [valid_flag (1 bit)] [reserved (15 bits)] [index (48 bits)]
    const qulonglong key = (static_cast<qulonglong>(isValid ? 1 : 0) << 63)
                         | (static_cast<qulonglong>(index) & 0x0000FFFFFFFFFFFFull);

    if (QImage* cached = thumbnailCache_.object(key)) {
        return *cached;
    }

    const std::string imgPath = isValid ? "/valid_frames/images" : "/invalid_frames/images";
    const std::string maskPath = isValid ? "/valid_frames/masks"  : "/invalid_frames/masks";

    QImage thumbImage;
    cv::Mat original;
    if (!hdfReader_ || !hdfReader_->readImageByIndex(imgPath, index, original)) {
        SPDLOG_DEBUG("buildThumbnailForIndex: missing original {}[{}]", imgPath, index);
        return thumbImage;
    }

    const auto& framesRef = isValid ? validFrames_ : invalidFrames_;
    const backend::services::FilterResult* validation = (index < framesRef.size()) ? &framesRef[index].validation : nullptr;
    if (overlayMode_ != OverlayMode::None) {
        cv::Mat mask;
        if (hdfReader_->readImageByIndex(maskPath, index, mask) && !mask.empty()) {
            thumbImage = createProcessingOverlay(original, mask, validation, overlayMode_);
        } else {
            thumbImage = matToQImage(original);
        }
    } else {
        thumbImage = matToQImage(original);
    }
    if (showRoiOverlay_ && !thumbImage.isNull() && roi_.w > 0 && roi_.h > 0) {
        thumbImage = drawRoiOverlay(thumbImage, original.cols, original.rows);
    }

    if (!thumbImage.isNull()) {
        QImage scaled = thumbImage.scaled(THUMBNAIL_SIZE, THUMBNAIL_SIZE,
                                          Qt::KeepAspectRatio, Qt::SmoothTransformation);
        auto* stored = new QImage(scaled);
        thumbnailCache_.insert(key, stored, 1);
        return scaled;
    }
    return thumbImage;
}

void HdfReviewTab::refreshVisibleThumbnails(bool isValid) {
    const auto& frames = isValid ? validFrames_ : invalidFrames_;
    if (frames.empty()) return;

    size_t startIndex = 0, endIndex = 0;
    computeVisibleRange(isValid, startIndex, endIndex);
    if (endIndex <= startIndex) return;

    QGridLayout* grid = isValid ? ui->validImageGrid : ui->invalidImageGrid;

    // Remove existing thumbnail labels
    QVector<QWidget*> toRemove;
    for (int i = 0; i < grid->count(); ++i) {
        QLayoutItem* it = grid->itemAt(i);
        if (!it) continue;
        QWidget* w = it->widget();
        if (w && qobject_cast<ThumbnailLabel*>(w)) {
            toRemove.push_back(w);
        }
    }
    for (QWidget* w : toRemove) {
        grid->removeWidget(w);
        w->deleteLater();
    }

    // Update top spacer height for rows before startIndex
    const int cellH = THUMBNAIL_SIZE + 8;
    const size_t totalRows = (frames.size() + GRID_COLUMNS - 1) / GRID_COLUMNS;
    const size_t startRow = startIndex / GRID_COLUMNS;
    const size_t visibleRows = ((endIndex - startIndex) + GRID_COLUMNS - 1) / GRID_COLUMNS;

    if (isValid) {
        if (validTopSpacer_) {
            ui->validImageGrid->removeItem(validTopSpacer_);
            delete validTopSpacer_;
        }
        validTopSpacer_ = new QSpacerItem(0, static_cast<int>(startRow) * cellH, QSizePolicy::Minimum, QSizePolicy::Fixed);
        ui->validImageGrid->addItem(validTopSpacer_, 0, 0, 1, GRID_COLUMNS);
    } else {
        if (invalidTopSpacer_) {
            ui->invalidImageGrid->removeItem(invalidTopSpacer_);
            delete invalidTopSpacer_;
        }
        invalidTopSpacer_ = new QSpacerItem(0, static_cast<int>(startRow) * cellH, QSizePolicy::Minimum, QSizePolicy::Fixed);
        ui->invalidImageGrid->addItem(invalidTopSpacer_, 0, 0, 1, GRID_COLUMNS);
    }

    // Add visible thumbnails as a contiguous block after the top spacer
    int localRowBase = 1; // row 0 is reserved for top spacer
    for (size_t i = startIndex; i < endIndex; ++i) {
        int localRow = localRowBase + static_cast<int>((i - startIndex) / GRID_COLUMNS);
        int col = static_cast<int>(i % GRID_COLUMNS);
        auto* label = new ThumbnailLabel(static_cast<int>(i), THUMBNAIL_SIZE, grid->parentWidget());
        QImage img = buildThumbnailForIndex(i, isValid);
        if (!img.isNull()) {
            label->setPixmap(QPixmap::fromImage(img));
        }
        grid->addWidget(label, localRow, col);
        connect(label, &ThumbnailLabel::clicked, this, &HdfReviewTab::onThumbnailClicked);
        connect(label, &ThumbnailLabel::doubleClicked, this, &HdfReviewTab::onThumbnailDoubleClicked);
    }

    // Adjust bottom spacer for rows after endIndex
    const size_t remainingRows = (totalRows > (startRow + visibleRows)) ? (totalRows - (startRow + visibleRows)) : 0;
    const int bottomH = static_cast<int>(remainingRows) * cellH;
    if (isValid) {
        if (validBottomSpacer_) {
            ui->validImageGrid->removeItem(validBottomSpacer_);
            delete validBottomSpacer_;
        }
        validBottomSpacer_ = new QSpacerItem(0, bottomH, QSizePolicy::Minimum, QSizePolicy::Fixed);
        ui->validImageGrid->addItem(validBottomSpacer_, localRowBase + static_cast<int>(visibleRows), 0, 1, GRID_COLUMNS);
    } else {
        if (invalidBottomSpacer_) {
            ui->invalidImageGrid->removeItem(invalidBottomSpacer_);
            delete invalidBottomSpacer_;
        }
        invalidBottomSpacer_ = new QSpacerItem(0, bottomH, QSizePolicy::Minimum, QSizePolicy::Fixed);
        ui->invalidImageGrid->addItem(invalidBottomSpacer_, localRowBase + static_cast<int>(visibleRows), 0, 1, GRID_COLUMNS);
    }
}

void HdfReviewTab::pruneOffscreenThumbnails(bool isValid) {
    const auto& frames = isValid ? validFrames_ : invalidFrames_;
    if (frames.empty()) return;

    size_t keepStart = 0, keepEnd = 0;
    computeVisibleRange(isValid, keepStart, keepEnd);
    if (keepEnd <= keepStart) return;

    QGridLayout* grid = isValid ? ui->validImageGrid : ui->invalidImageGrid;

    // Collect labels to remove (outside keep range)
    QVector<QWidget*> toRemove;
    for (int i = 0; i < grid->count(); ++i) {
        QLayoutItem* it = grid->itemAt(i);
        if (!it) continue;
        QWidget* w = it->widget();
        if (!w) continue; // skip non-widget items like QSpacerItem
        auto* label = qobject_cast<ThumbnailLabel*>(w);
        if (!label) continue;
        const size_t idx = static_cast<size_t>(label->frameIndex());
        if (idx < keepStart || idx >= keepEnd) {
            toRemove.push_back(w);
        }
    }
    for (QWidget* w : toRemove) {
        grid->removeWidget(w);
        w->deleteLater();
    }

    // Recompute bottom spacer height based on max index currently present
    size_t maxIndexPresent = 0;
    bool any = false;
    for (int i = 0; i < grid->count(); ++i) {
        QLayoutItem* it = grid->itemAt(i);
        if (!it) continue;
        QWidget* w = it->widget();
        auto* label = qobject_cast<ThumbnailLabel*>(w);
        if (!label) continue;
        any = true;
        size_t idx = static_cast<size_t>(label->frameIndex());
        if (idx > maxIndexPresent) maxIndexPresent = idx;
    }
    const size_t totalRows = (frames.size() + GRID_COLUMNS - 1) / GRID_COLUMNS;
    size_t loadedRows = any ? ((maxIndexPresent + 1 + GRID_COLUMNS - 1) / GRID_COLUMNS) : 0;
    const int cellH = THUMBNAIL_SIZE + 8;
    const int remainingRows = static_cast<int>(totalRows > loadedRows ? (totalRows - loadedRows) : 0);
    const int spacerH = remainingRows * cellH;
    if (isValid) {
        if (validBottomSpacer_) {
            ui->validImageGrid->removeItem(validBottomSpacer_);
            delete validBottomSpacer_;
        }
        validBottomSpacer_ = new QSpacerItem(0, spacerH, QSizePolicy::Minimum, QSizePolicy::Fixed);
        ui->validImageGrid->addItem(validBottomSpacer_, static_cast<int>(loadedRows), 0, 1, GRID_COLUMNS);
    } else {
        if (invalidBottomSpacer_) {
            ui->invalidImageGrid->removeItem(invalidBottomSpacer_);
            delete invalidBottomSpacer_;
        }
        invalidBottomSpacer_ = new QSpacerItem(0, spacerH, QSizePolicy::Minimum, QSizePolicy::Fixed);
        ui->invalidImageGrid->addItem(invalidBottomSpacer_, static_cast<int>(loadedRows), 0, 1, GRID_COLUMNS);
    }
}

void HdfReviewTab::showFrameViewer(int frameIndex) {
    const auto& framesMeta = isShowingValid_ ? validFrames_ : invalidFrames_;
    if (frameIndex < 0 || frameIndex >= static_cast<int>(framesMeta.size())) {
        return;
    }
    SPDLOG_INFO("HdfReviewTab: showFrameViewer index={} ({})", frameIndex, isShowingValid_ ? "valid" : "invalid");

    // Build a full ProcessedFrame by fetching images on demand
    backend::services::ProcessedFrame initialFrame = framesMeta[frameIndex];
    const std::string imgPath = isShowingValid_ ? "/valid_frames/images" : "/invalid_frames/images";
    const std::string maskPath = isShowingValid_ ? "/valid_frames/masks"  : "/invalid_frames/masks";

    if (hdfReader_) {
        cv::Mat original, mask;
        if (hdfReader_->readImageByIndex(imgPath, static_cast<size_t>(frameIndex), original)) {
            initialFrame.originalImage = original;
            SPDLOG_TRACE("HdfReviewTab: viewer loaded original {}[{}] ({}x{}x{})",
                         imgPath, frameIndex, original.cols, original.rows, original.channels());
        }
        if (hdfReader_->readImageByIndex(maskPath, static_cast<size_t>(frameIndex), mask)) {
            initialFrame.processedImage = mask;
            SPDLOG_TRACE("HdfReviewTab: viewer loaded mask {}[{}] ({}x{}x{})",
                         maskPath, frameIndex, mask.cols, mask.rows, mask.channels());
        }
        // Load multi-image series data if available (valid frames only)
        if (isShowingValid_) {
            std::vector<cv::Mat> seriesImages;
            if (hdfReader_->readSeriesImagesByIndex(static_cast<size_t>(frameIndex), seriesImages) && !seriesImages.empty()) {
                initialFrame.seriesImages = std::move(seriesImages);
                SPDLOG_DEBUG("HdfReviewTab: loaded {} series images for frame {}", initialFrame.seriesImages.size(), frameIndex);
            }
        }
    }

    // Create dialog with current overlay mode and ROI overlay state
    auto* dialog = new FrameViewerDialog(initialFrame, roi_, overlayMode_, showRoiOverlay_, this);
    
    // Store current index in a way that can be modified by lambdas
    struct NavigationState {
        int currentIndex;
        bool isValidSet;
    };

    auto* navState = new NavigationState{frameIndex, isShowingValid_};
    
    // Connect navigation signals
    // Helper lambda to load series images for a frame
    auto loadSeriesImages = [this, navState](backend::services::ProcessedFrame& pf, int idx) {
        if (navState->isValidSet && hdfReader_) {
            std::vector<cv::Mat> seriesImages;
            if (hdfReader_->readSeriesImagesByIndex(static_cast<size_t>(idx), seriesImages) && !seriesImages.empty()) {
                pf.seriesImages = std::move(seriesImages);
            }
        }
    };

    connect(dialog, &FrameViewerDialog::requestPreviousFrame, this, [this, dialog, navState, loadSeriesImages]() {
        const auto& frames = navState->isValidSet ? validFrames_ : invalidFrames_;
        if (frames.empty()) return;
        navState->currentIndex = navState->currentIndex - 1;
        if (navState->currentIndex < 0) {
            navState->currentIndex = static_cast<int>(frames.size()) - 1; // Wrap to last
        }
        if (navState->currentIndex >= 0 && navState->currentIndex < static_cast<int>(frames.size())) {
            // Fetch images on demand
            backend::services::ProcessedFrame pf = frames[navState->currentIndex];
            const std::string imgPath2 = navState->isValidSet ? "/valid_frames/images" : "/invalid_frames/images";
            const std::string maskPath2 = navState->isValidSet ? "/valid_frames/masks"  : "/invalid_frames/masks";
            if (hdfReader_) {
                cv::Mat original2, mask2;
                if (hdfReader_->readImageByIndex(imgPath2, static_cast<size_t>(navState->currentIndex), original2)) {
                    pf.originalImage = original2;
                }
                if (hdfReader_->readImageByIndex(maskPath2, static_cast<size_t>(navState->currentIndex), mask2)) {
                    pf.processedImage = mask2;
                }
            }
            loadSeriesImages(pf, navState->currentIndex);
            dialog->setFrame(pf);
            // Update selected frame in main view
            setSelectedFrame(navState->currentIndex);
        }
    });

    connect(dialog, &FrameViewerDialog::requestNextFrame, this, [this, dialog, navState, loadSeriesImages]() {
        const auto& frames = navState->isValidSet ? validFrames_ : invalidFrames_;
        if (frames.empty()) return;
        navState->currentIndex = navState->currentIndex + 1;
        if (navState->currentIndex >= static_cast<int>(frames.size())) {
            navState->currentIndex = 0; // Wrap to first
        }
        if (navState->currentIndex >= 0 && navState->currentIndex < static_cast<int>(frames.size())) {
            backend::services::ProcessedFrame pf = frames[navState->currentIndex];
            const std::string imgPath2 = navState->isValidSet ? "/valid_frames/images" : "/invalid_frames/images";
            const std::string maskPath2 = navState->isValidSet ? "/valid_frames/masks"  : "/invalid_frames/masks";
            if (hdfReader_) {
                cv::Mat original2, mask2;
                if (hdfReader_->readImageByIndex(imgPath2, static_cast<size_t>(navState->currentIndex), original2)) {
                    pf.originalImage = original2;
                }
                if (hdfReader_->readImageByIndex(maskPath2, static_cast<size_t>(navState->currentIndex), mask2)) {
                    pf.processedImage = mask2;
                }
            }
            loadSeriesImages(pf, navState->currentIndex);
            dialog->setFrame(pf);
            // Update selected frame in main view
            setSelectedFrame(navState->currentIndex);
        }
    });
    
    // Clean up navigation state when dialog is destroyed
    connect(dialog, &QObject::destroyed, this, [navState]() {
        delete navState;
    });
    
    // Show dialog
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->exec();
}

void HdfReviewTab::onExportAll() {
    if (!hdfReader_ || (validFrames_.empty() && invalidFrames_.empty())) {
        QMessageBox::warning(this, tr("Export Error"),
                            tr("No data available to export."));
        return;
    }

    QString dirPath = QFileDialog::getExistingDirectory(this, tr("Select Directory to Export All Data"),
                                                        "", QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dirPath.isEmpty()) {
        return;
    }

    exportAllData(dirPath);
}

void HdfReviewTab::onExportCharts() {
    if (validFrames_.empty() && invalidFrames_.empty()) {
        QMessageBox::warning(this, tr("Export Error"),
                            tr("No data available to export charts."));
        return;
    }

    QString dirPath = QFileDialog::getExistingDirectory(this, tr("Select Directory to Export Charts"),
                                                        "", QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dirPath.isEmpty()) {
        return;
    }

    QDir dir(dirPath);
    bool success = true;
    
    // Generate charts from current data
    generateScatterPlot(validFrames_);
    generateHistogram(validFrames_);
    
    // Export scatter plot
    QString scatterPath = dir.filePath("scatter_plot.tiff");
    QPixmap scatterPixmap = chartToPixmap(scatterPlotView_);
    if (!scatterPixmap.isNull()) {
        QImage scatterImage = scatterPixmap.toImage();
        // Convert to RGB32 format for consistent handling
        scatterImage = scatterImage.convertToFormat(QImage::Format_RGB32);
        cv::Mat scatterMat(scatterImage.height(), scatterImage.width(), CV_8UC4, 
                          const_cast<uchar*>(scatterImage.constBits()), 
                          scatterImage.bytesPerLine());
        cv::Mat scatterBGR;
        cv::cvtColor(scatterMat, scatterBGR, cv::COLOR_RGBA2BGR);
        if (!cv::imwrite(scatterPath.toStdString(), scatterBGR)) {
            SPDLOG_WARN("Failed to write scatter plot TIFF: {}", scatterPath.toStdString());
            success = false;
        } else {
            SPDLOG_INFO("Exported scatter plot {}x{} to {}", scatterBGR.cols, scatterBGR.rows, scatterPath.toStdString());
        }
    } else {
        success = false;
    }
    
    // Export histogram
    QString histogramPath = dir.filePath("ring_width_histogram.tiff");
    QPixmap histogramPixmap = chartToPixmap(histogramView_);
    if (!histogramPixmap.isNull()) {
        QImage histogramImage = histogramPixmap.toImage();
        // Convert to RGB32 format for consistent handling
        histogramImage = histogramImage.convertToFormat(QImage::Format_RGB32);
        cv::Mat histogramMat(histogramImage.height(), histogramImage.width(), CV_8UC4, 
                            const_cast<uchar*>(histogramImage.constBits()), 
                            histogramImage.bytesPerLine());
        cv::Mat histogramBGR;
        cv::cvtColor(histogramMat, histogramBGR, cv::COLOR_RGBA2BGR);
        if (!cv::imwrite(histogramPath.toStdString(), histogramBGR)) {
            SPDLOG_WARN("Failed to write histogram TIFF: {}", histogramPath.toStdString());
            success = false;
        } else {
            SPDLOG_INFO("Exported histogram {}x{} to {}", histogramBGR.cols, histogramBGR.rows, histogramPath.toStdString());
        }
    } else {
        success = false;
    }
    
    if (success) {
        QMessageBox::information(this, tr("Export Complete"),
                                tr("Charts exported successfully to:\n%1").arg(dirPath));
    } else {
        QMessageBox::warning(this, tr("Export Warning"),
                            tr("Some charts may not have been exported."));
    }
}

void HdfReviewTab::exportAllImagesToTiff(const QString& baseDir) {
    if (!hdfReader_) {
        return;
    }

    QDir dir(baseDir);
    if (!dir.exists()) {
        QMessageBox::critical(this, tr("Export Error"),
                              tr("Directory does not exist: %1").arg(baseDir));
        return;
    }

    int exportedCount = 0;
    int seriesExportedCount = 0;
    int totalCount = static_cast<int>(validFrames_.size() + invalidFrames_.size());

    // Check if series images are available
    size_t seriesCount = 0, seriesRecords = 0;
    int seriesH = 0, seriesW = 0;
    bool hasSeriesImages = hdfReader_->getSeriesImageInfo(seriesRecords, seriesCount, seriesH, seriesW);

    // Export valid frames
    for (size_t i = 0; i < validFrames_.size(); ++i) {
        cv::Mat image;
        if (hdfReader_->readImageByIndex("/valid_frames/images", i, image)) {
            QString fileName = QString("valid_frame_%1.tiff").arg(validFrames_[i].index, 6, 10, QChar('0'));
            QString filePath = dir.filePath(fileName);

            // Export without compression
            if (cv::imwrite(filePath.toStdString(), image)) {
                exportedCount++;
            }
        }

        // Export series images if available
        if (hasSeriesImages && i < seriesRecords) {
            std::vector<cv::Mat> seriesImages;
            if (hdfReader_->readSeriesImagesByIndex(i, seriesImages)) {
                for (size_t s = 0; s < seriesImages.size(); ++s) {
                    QString seriesFileName = QString("valid_frame_%1_series_%2.tiff")
                        .arg(validFrames_[i].index, 6, 10, QChar('0'))
                        .arg(s, 2, 10, QChar('0'));
                    QString seriesFilePath = dir.filePath(seriesFileName);
                    if (cv::imwrite(seriesFilePath.toStdString(), seriesImages[s])) {
                        seriesExportedCount++;
                    }
                }
            }
        }
    }

    // Export invalid frames
    for (size_t i = 0; i < invalidFrames_.size(); ++i) {
        cv::Mat image;
        if (hdfReader_->readImageByIndex("/invalid_frames/images", i, image)) {
            QString fileName = QString("invalid_frame_%1.tiff").arg(invalidFrames_[i].index, 6, 10, QChar('0'));
            QString filePath = dir.filePath(fileName);

            // Export without compression
            if (cv::imwrite(filePath.toStdString(), image)) {
                exportedCount++;
            }
        }
    }

    QString message = tr("Exported %1 of %2 images to:\n%3")
        .arg(exportedCount).arg(totalCount).arg(baseDir);
    if (seriesExportedCount > 0) {
        message += tr("\n+ %1 series images").arg(seriesExportedCount);
    }
    QMessageBox::information(this, tr("Export Complete"), message);
    SPDLOG_INFO("Exported {} of {} images + {} series images to {}", exportedCount, totalCount, seriesExportedCount, baseDir.toStdString());
}

bool HdfReviewTab::exportChartFromHdf5(const std::string& datasetPath, const QString& filePath) {
    if (!hdfReader_) {
        return false;
    }

    cv::Mat chartImage;
    if (!hdfReader_->readImageByIndex(datasetPath, 0, chartImage)) {
        SPDLOG_WARN("Failed to read chart from HDF5: {}", datasetPath);
        return false;
    }

    // Charts are saved as BGR, which is what OpenCV imwrite expects
    // Export without compression
    if (!cv::imwrite(filePath.toStdString(), chartImage)) {
        SPDLOG_ERROR("Failed to write chart TIFF: {}", filePath.toStdString());
        return false;
    }

    SPDLOG_INFO("Exported chart from {} to {}", datasetPath, filePath.toStdString());
    return true;
}

void HdfReviewTab::exportAllData(const QString& baseDir) {
    if (!hdfReader_) {
        return;
    }

    QDir dir(baseDir);
    if (!dir.exists()) {
        QMessageBox::critical(this, tr("Export Error"),
                              tr("Directory does not exist: %1").arg(baseDir));
        return;
    }

    int exportedImages = 0;
    int totalImages = static_cast<int>(validFrames_.size() + invalidFrames_.size());
    bool csvExported = false;
    bool chartsExported = true;

    // Export CSV metrics
    QString csvPath = dir.filePath("metrics.csv");
    exportMetricsToCsv(csvPath);
    csvExported = QFile::exists(csvPath);

    // Export all images
    // Export valid frames
    for (size_t i = 0; i < validFrames_.size(); ++i) {
        cv::Mat image;
        if (hdfReader_->readImageByIndex("/valid_frames/images", i, image)) {
            QString fileName = QString("valid_frame_%1.tiff").arg(validFrames_[i].index, 6, 10, QChar('0'));
            QString filePath = dir.filePath(fileName);

            // Export without compression
            if (cv::imwrite(filePath.toStdString(), image)) {
                exportedImages++;
            }
        }
    }

    // Export invalid frames
    for (size_t i = 0; i < invalidFrames_.size(); ++i) {
        cv::Mat image;
        if (hdfReader_->readImageByIndex("/invalid_frames/images", i, image)) {
            QString fileName = QString("invalid_frame_%1.tiff").arg(invalidFrames_[i].index, 6, 10, QChar('0'));
            QString filePath = dir.filePath(fileName);

            // Export without compression
            if (cv::imwrite(filePath.toStdString(), image)) {
                exportedImages++;
            }
        }
    }

    // Generate and export charts from current data
    generateScatterPlot(validFrames_);
    generateHistogram(validFrames_);
    
    QString scatterPath = dir.filePath("scatter_plot.tiff");
    QPixmap scatterPixmap = chartToPixmap(scatterPlotView_);
    if (!scatterPixmap.isNull()) {
        QImage scatterImage = scatterPixmap.toImage();
        // Convert to RGB32 format for consistent handling
        scatterImage = scatterImage.convertToFormat(QImage::Format_RGB32);
        cv::Mat scatterMat(scatterImage.height(), scatterImage.width(), CV_8UC4, 
                          const_cast<uchar*>(scatterImage.constBits()), 
                          scatterImage.bytesPerLine());
        cv::Mat scatterBGR;
        cv::cvtColor(scatterMat, scatterBGR, cv::COLOR_RGBA2BGR);
        if (!cv::imwrite(scatterPath.toStdString(), scatterBGR)) {
            chartsExported = false;
        } else {
            SPDLOG_INFO("Exported scatter plot {}x{} to {}", scatterBGR.cols, scatterBGR.rows, scatterPath.toStdString());
        }
    } else {
        chartsExported = false;
    }
    
    QString histogramPath = dir.filePath("ring_width_histogram.tiff");
    QPixmap histogramPixmap = chartToPixmap(histogramView_);
    if (!histogramPixmap.isNull()) {
        QImage histogramImage = histogramPixmap.toImage();
        // Convert to RGB32 format for consistent handling
        histogramImage = histogramImage.convertToFormat(QImage::Format_RGB32);
        cv::Mat histogramMat(histogramImage.height(), histogramImage.width(), CV_8UC4, 
                            const_cast<uchar*>(histogramImage.constBits()), 
                            histogramImage.bytesPerLine());
        cv::Mat histogramBGR;
        cv::cvtColor(histogramMat, histogramBGR, cv::COLOR_RGBA2BGR);
        if (!cv::imwrite(histogramPath.toStdString(), histogramBGR)) {
            chartsExported = false;
        } else {
            SPDLOG_INFO("Exported histogram {}x{} to {}", histogramBGR.cols, histogramBGR.rows, histogramPath.toStdString());
        }
    } else {
        chartsExported = false;
    }

    QString message = tr("Export complete:\n");
    message += tr("- CSV: %1\n").arg(csvExported ? tr("Yes") : tr("No"));
    message += tr("- Images: %1 of %2\n").arg(exportedImages).arg(totalImages);
    message += tr("- Charts: %1\n").arg(chartsExported ? tr("Yes") : tr("Partial/No"));
    message += tr("\nLocation: %1").arg(baseDir);

    QMessageBox::information(this, tr("Export Complete"), message);
    SPDLOG_INFO("Exported all data: CSV={}, Images={}/{}, Charts={}, Location={}",
                csvExported, exportedImages, totalImages, chartsExported, baseDir.toStdString());
}

void HdfReviewTab::updateCharts() {
    if (!scatterPlotChart_ || !histogramChart_) {
        SPDLOG_WARN("HdfReviewTab::updateCharts: chart widgets are null");
        return;
    }

    // Generate charts from loaded frame data
    generateScatterPlot(validFrames_);
    generateHistogram(validFrames_);
    
    // Reload isoelastic curves if they were cleared (e.g., after clearDisplay)
    if (isoelasticCurves_.empty()) {
        loadIsoelasticCurves();
    }
    
    SPDLOG_INFO("HdfReviewTab::updateCharts: Generated charts from {} valid frames", validFrames_.size());
}

void HdfReviewTab::generateScatterPlot(const std::vector<backend::services::ProcessedFrame>& validFrames) {
    if (!scatterSeries_ || !scatterXAxis_ || !scatterYAxis_) {
        return;
    }

    scatterSeries_->clear();

    if (validFrames.empty()) {
        scatterXAxis_->setRange(0, 1000);
        scatterYAxis_->setRange(0, 1);
        return;
    }

    // Get conversion factor from backend (pixels to microns)
    const double conversionFactor = backend_.processing().getPixelToMicronFactor();
    // Area conversion: pixels² to microns² = pixels² * (microns/pixel)²
    const double areaConversionFactor = conversionFactor * conversionFactor;

    // Collect points
    std::vector<std::pair<double, double>> points;
    double minArea = std::numeric_limits<double>::max();
    double maxArea = std::numeric_limits<double>::lowest();
    double minDeform = std::numeric_limits<double>::max();
    double maxDeform = std::numeric_limits<double>::lowest();

    for (const auto& frame : validFrames) {
        if (frame.validation.isValid) {
            // Convert area from pixels² to microns²
            double areaPixels = frame.validation.area;
            double areaMicrons = areaPixels * areaConversionFactor;
            double deform = frame.validation.deformability;
            points.push_back({areaMicrons, deform});

            minArea = std::min(minArea, areaMicrons);
            maxArea = std::max(maxArea, areaMicrons);
            minDeform = std::min(minDeform, deform);
            maxDeform = std::max(maxDeform, deform);
        }
    }

    if (points.empty()) {
        scatterXAxis_->setRange(0, 1000);
        scatterYAxis_->setRange(0, 1);
        return;
    }

    // Add scatter points
    for (const auto& p : points) {
        scatterSeries_->append(p.first, p.second);
    }

    // Set axis ranges with padding
    if (minArea < maxArea) {
        double areaPadding = (maxArea - minArea) * 0.1;
        scatterXAxis_->setRange(minArea - areaPadding, maxArea + areaPadding);
    } else {
        scatterXAxis_->setRange(0, 1000);
    }

    if (minDeform < maxDeform) {
        double deformPadding = (maxDeform - minDeform) * 0.1;
        scatterYAxis_->setRange(minDeform - deformPadding, maxDeform + deformPadding);
    } else {
        scatterYAxis_->setRange(0, 1);
    }
}

void HdfReviewTab::generateHistogram(const std::vector<backend::services::ProcessedFrame>& validFrames) {
    // Use config range so the histogram matches the current ring ratio thresholds
    auto cfg = backend_.processing().getProcessingConfig();
    const double HISTOGRAM_MIN = cfg.ring_ratio_min;
    const double HISTOGRAM_MAX = cfg.ring_ratio_max;
    constexpr double HISTOGRAM_BIN_WIDTH = 0.5;
    const int HISTOGRAM_BINS = std::max(1, static_cast<int>((HISTOGRAM_MAX - HISTOGRAM_MIN) / HISTOGRAM_BIN_WIDTH));

    // Reset series
#if MIB_HAS_QHISTOGRAMSERIES
    if (histogramSeries_) {
        histogramSeries_->clear();
    }
#else
    if (histogramBarSeries_) {
        histogramBarSeries_->clear();
    }
#endif

    // Always set fixed x-axis range regardless of data
#if MIB_HAS_QHISTOGRAMSERIES
    if (histogramXAxis_) {
        histogramXAxis_->setRange(HISTOGRAM_MIN, HISTOGRAM_MAX);
        histogramXAxis_->setTickCount(6);
    }
#endif

    // Collect ring ratio values from valid frames
    std::vector<double> ringRatios;
    for (const auto& frame : validFrames) {
        if (frame.validation.isValid && frame.validation.ringRatio > 0.0) {
            ringRatios.push_back(frame.validation.ringRatio);
        }
    }

    // If no data, show empty histogram with fixed range
    if (ringRatios.empty()) {
        if (histogramYAxis_) {
            histogramYAxis_->setRange(0, 1);
        }
#if !MIB_HAS_QHISTOGRAMSERIES
        if (histogramCategoryAxis_) {
            histogramChart_->removeAxis(histogramCategoryAxis_);
            delete histogramCategoryAxis_;
            histogramCategoryAxis_ = nullptr;
        }
        histogramCategoryAxis_ = new QBarCategoryAxis();
        QStringList categories;
        categories.reserve(HISTOGRAM_BINS);
        for (int i = 0; i < HISTOGRAM_BINS; ++i) {
            const double start = HISTOGRAM_MIN + i * HISTOGRAM_BIN_WIDTH;
            const double end = (i == HISTOGRAM_BINS - 1) ? HISTOGRAM_MAX : (start + HISTOGRAM_BIN_WIDTH);
            categories << QString("%1-%2").arg(start, 0, 'f', 1).arg(end, 0, 'f', 1);
        }
        histogramCategoryAxis_->append(categories);
        histogramCategoryAxis_->setLabelsAngle(-90);
        histogramChart_->addAxis(histogramCategoryAxis_, Qt::AlignBottom);
        if (histogramBarSeries_) {
            histogramBarSeries_->attachAxis(histogramCategoryAxis_);
        }
#endif
        return;
    }

    // Count values in each bin
    std::vector<int> binCounts(HISTOGRAM_BINS, 0);
    for (double val : ringRatios) {
        double clampedVal = std::clamp(val, HISTOGRAM_MIN, HISTOGRAM_MAX);
        int binIndex = static_cast<int>((clampedVal - HISTOGRAM_MIN) / HISTOGRAM_BIN_WIDTH);
        if (binIndex >= HISTOGRAM_BINS) {
            binIndex = HISTOGRAM_BINS - 1;
        }
        binIndex = std::clamp(binIndex, 0, HISTOGRAM_BINS - 1);
        binCounts[binIndex]++;
    }

    int maxCount = 0;
    for (int count : binCounts) {
        maxCount = std::max(maxCount, count);
    }

    // Set Y-axis range
    if (histogramYAxis_) {
        const int yMax = std::max(1, static_cast<int>(std::ceil(maxCount * 1.1)));
        histogramYAxis_->setRange(0, yMax);
        histogramYAxis_->applyNiceNumbers();
    }

#if MIB_HAS_QHISTOGRAMSERIES
    // Populate histogram series
    if (histogramSeries_) {
        QVector<qreal> samples;
        samples.reserve(static_cast<int>(ringRatios.size()));
        for (double v : ringRatios) {
            samples.append(static_cast<qreal>(v));
        }
        histogramSeries_->setBinsCount(HISTOGRAM_BINS);
        histogramSeries_->setSamples(samples);
    }
#else
    // Fallback: build bar set and category axis
    auto* barSet = new QBarSet("");
    for (int count : binCounts) {
        *barSet << count;
    }
    if (histogramBarSeries_) {
        histogramBarSeries_->append(barSet);
    }
    
    if (histogramCategoryAxis_) {
        histogramChart_->removeAxis(histogramCategoryAxis_);
        delete histogramCategoryAxis_;
        histogramCategoryAxis_ = nullptr;
    }
    histogramCategoryAxis_ = new QBarCategoryAxis();
    QStringList categories;
    categories.reserve(HISTOGRAM_BINS);
    for (int i = 0; i < HISTOGRAM_BINS; ++i) {
        const double start = HISTOGRAM_MIN + i * HISTOGRAM_BIN_WIDTH;
        const double end = (i == HISTOGRAM_BINS - 1) ? HISTOGRAM_MAX : (start + HISTOGRAM_BIN_WIDTH);
        categories << QString("%1-%2").arg(start, 0, 'f', 1).arg(end, 0, 'f', 1);
    }
    histogramCategoryAxis_->append(categories);
    histogramCategoryAxis_->setLabelsAngle(-90);
    histogramChart_->addAxis(histogramCategoryAxis_, Qt::AlignBottom);
    if (histogramBarSeries_) {
        histogramBarSeries_->attachAxis(histogramCategoryAxis_);
    }
#endif
}

QPixmap HdfReviewTab::chartToPixmap(QChartView* chartView) const {
    if (!chartView || !chartView->chart()) {
        return QPixmap();
    }
    
    // Render chart at high resolution for export (square format: 1200x1200 pixels)
    const int exportSize = 1200;
    
    // Save original chart view size and minimum size
    QSize originalSize = chartView->size();
    QSize originalMinSize = chartView->minimumSize();
    
    // Temporarily set minimum size and resize the chart view to match export size
    // This ensures the chart layout is correct for the export dimensions
    chartView->setMinimumSize(exportSize, exportSize);
    chartView->resize(exportSize, exportSize);
    
    // Force layout update and rendering
    chartView->updateGeometry();
    chartView->update();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    
    // Grab the chart at the new size
    QPixmap pixmap = chartView->grab();
    
    // Restore original size and minimum size
    chartView->setMinimumSize(originalMinSize);
    chartView->resize(originalSize);
    chartView->update();
    
    return pixmap;
}

void HdfReviewTab::loadIsoelasticCurves() {
    // Clear any existing curves to avoid duplicates
    for (auto it = isoelasticCurves_.begin(); it != isoelasticCurves_.end(); ++it) {
        QLineSeries* series = *it;
        if (series) {
            scatterPlotChart_->removeSeries(series);
            delete series;
        }
    }
    isoelasticCurves_.clear();
    
    // Find the isoelastic curve data file
    QString appDir = QCoreApplication::applicationDirPath();
    QString filePath = QDir(appDir).absoluteFilePath("../resources/isoelastic_curve/scaled_isoelastic_data_6.16-4.24.txt");
    
    // Try alternative path if file doesn't exist
    if (!QFile::exists(filePath)) {
        filePath = QDir(appDir).absoluteFilePath("resources/isoelastic_curve/scaled_isoelastic_data_6.16-4.24.txt");
    }
    
    // Try source directory path for development
    if (!QFile::exists(filePath)) {
        filePath = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../../resources/isoelastic_curve/scaled_isoelastic_data_6.16-4.24.txt");
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        SPDLOG_WARN("Failed to open isoelastic curve file: {}", filePath.toStdString());
        return;
    }

    // Group data points by emodulus value
    std::map<double, std::vector<std::pair<double, double>>> curvesByModulus;

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        
        // Skip empty lines and comments
        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }

        // Parse tab-separated values: area_um, deform, emodulus
        QStringList parts = line.split('\t', Qt::SkipEmptyParts);
        if (parts.size() < 3) {
            continue;
        }

        bool ok1, ok2, ok3;
        double areaUm = parts[0].toDouble(&ok1);
        double deform = parts[1].toDouble(&ok2);
        double emodulus = parts[2].toDouble(&ok3);

        if (ok1 && ok2 && ok3) {
            curvesByModulus[emodulus].push_back({areaUm, deform});
        }
    }

    file.close();

    if (curvesByModulus.empty()) {
        SPDLOG_WARN("No isoelastic curve data found in file: {}", filePath.toStdString());
        return;
    }

    // Create QLineSeries for each modulus value (in reverse order for legend)
    for (auto it = curvesByModulus.rbegin(); it != curvesByModulus.rend(); ++it) {
        const auto& [emodulus, points] = *it;
        QLineSeries* series = new QLineSeries();
        series->setName(QString("%1 kPa").arg(emodulus, 0, 'f', 2));
        
        // Add points to series
        for (const auto& [area, deform] : points) {
            series->append(area, deform);
        }

        // Add series to chart
        scatterPlotChart_->addSeries(series);
        series->attachAxis(scatterXAxis_);
        series->attachAxis(scatterYAxis_);
        
        // Store pointer for cleanup
        isoelasticCurves_.push_back(series);
    }

    // Enable legend to show all series and position it on the right
    scatterPlotChart_->legend()->setVisible(true);
    scatterPlotChart_->legend()->setAlignment(Qt::AlignRight);
    
    SPDLOG_INFO("Loaded {} isoelastic curves from {}", curvesByModulus.size(), filePath.toStdString());
}

} // namespace frontend

// Include moc file for ThumbnailLabel class (defined in this .cpp file with Q_OBJECT)
#include "HdfReviewTab.moc"


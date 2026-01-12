#include "frontend/HdfReviewTab.h"

#include <QPushButton>
#include <QLabel>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableView>
#include <QGridLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QHeaderView>
#include <QTableWidgetItem>
#include <QMouseEvent>
#include <QPainter>
#include <QFrame>
#include <QSpacerItem>
#include <QFile>
#include <QTextStream>
#include <QCheckBox>
#include <algorithm>

#include "backend/AppBackend.h"
#include "backend/services/Hdf5Service.h"
#include "backend/services/ProcessingService.h"
#include "frontend/FrameViewerDialog.h"
#include "frontend/HdfMetricsModel.h"

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
    : QWidget(parent), backend_(backend) {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(6, 6, 6, 6);
    rootLayout->setSpacing(6);

    // Configure thumbnail cache (store up to ~2048 thumbnails)
    thumbnailCache_.setMaxCost(2048);
    SPDLOG_INFO("HdfReviewTab: thumbnail cache size set to {}", 2048);

    // File selection row
    auto* fileRow = new QHBoxLayout();
    selectFileBtn_ = new QPushButton(tr("Select HDF File..."), this);
    exportMetricsBtn_ = new QPushButton(tr("Export Metrics to CSV..."), this);
    exportMetricsBtn_->setEnabled(false);
    exportAllBtn_ = new QPushButton(tr("Export All..."), this);
    exportAllBtn_->setEnabled(false);
    exportChartsBtn_ = new QPushButton(tr("Export Charts as TIFF..."), this);
    exportChartsBtn_->setEnabled(false);
    roiOverlayCheck_ = new QCheckBox(tr("Show Overlays"), this);
    roiOverlayCheck_->setEnabled(false);
    filePathLabel_ = new QLabel(tr("No file selected"), this);
    statusLabel_ = new QLabel(tr("Ready"), this);
    fileRow->addWidget(selectFileBtn_);
    fileRow->addWidget(exportMetricsBtn_);
    fileRow->addWidget(exportAllBtn_);
    fileRow->addWidget(exportChartsBtn_);
    fileRow->addWidget(roiOverlayCheck_);
    fileRow->addWidget(filePathLabel_, 1);
    fileRow->addWidget(statusLabel_);
    rootLayout->addLayout(fileRow);

    connect(selectFileBtn_, &QPushButton::clicked, this, &HdfReviewTab::onSelectFile);
    connect(exportMetricsBtn_, &QPushButton::clicked, this, &HdfReviewTab::onExportMetrics);
    connect(exportAllBtn_, &QPushButton::clicked, this, &HdfReviewTab::onExportAll);
    connect(exportChartsBtn_, &QPushButton::clicked, this, &HdfReviewTab::onExportCharts);
    connect(roiOverlayCheck_, &QCheckBox::toggled, this, &HdfReviewTab::onToggleRoiOverlay);

    // Tab widget for valid/invalid frames
    frameTypeTabs_ = new QTabWidget(this);
    
    // Valid frames tab
    validFramesWidget_ = new QWidget(this);
    auto* validLayout = new QHBoxLayout(validFramesWidget_);
    validLayout->setContentsMargins(0, 0, 0, 0);
    
    validImageScroll_ = new QScrollArea(validFramesWidget_);
    validImageGridWidget_ = new QWidget(validImageScroll_);
    validImageGrid_ = new QGridLayout(validImageGridWidget_);
    validImageGrid_->setSpacing(4);
    validImageScroll_->setWidget(validImageGridWidget_);
    validImageScroll_->setWidgetResizable(true);
    validImageScroll_->setMinimumWidth(400);
    validImageScroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    validBottomSpacer_ = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Fixed);
    validImageGrid_->addItem(validBottomSpacer_, 0, 0, 1, GRID_COLUMNS);
    connect(validImageScroll_->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &HdfReviewTab::onScrollValueChanged);
    
    validMetricsTable_ = new QTableView(validFramesWidget_);
    validMetricsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    validMetricsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    validMetricsTable_->horizontalHeader()->setStretchLastSection(true);
    validMetricsModel_ = new HdfMetricsModel(validMetricsTable_);
    validMetricsModel_->setSource(&validFrames_);
    validMetricsTable_->setModel(validMetricsModel_);
    
    validLayout->addWidget(validImageScroll_, 1);
    validLayout->addWidget(validMetricsTable_, 1);
    
    frameTypeTabs_->addTab(validFramesWidget_, tr("Valid Frames"));

    // Invalid frames tab
    invalidFramesWidget_ = new QWidget(this);
    auto* invalidLayout = new QHBoxLayout(invalidFramesWidget_);
    invalidLayout->setContentsMargins(0, 0, 0, 0);
    
    invalidImageScroll_ = new QScrollArea(invalidFramesWidget_);
    invalidImageGridWidget_ = new QWidget(invalidImageScroll_);
    invalidImageGrid_ = new QGridLayout(invalidImageGridWidget_);
    invalidImageGrid_->setSpacing(4);
    invalidImageScroll_->setWidget(invalidImageGridWidget_);
    invalidImageScroll_->setWidgetResizable(true);
    invalidImageScroll_->setMinimumWidth(400);
    invalidImageScroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    invalidBottomSpacer_ = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Fixed);
    invalidImageGrid_->addItem(invalidBottomSpacer_, 0, 0, 1, GRID_COLUMNS);
    connect(invalidImageScroll_->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &HdfReviewTab::onScrollValueChanged);
    
    invalidMetricsTable_ = new QTableView(invalidFramesWidget_);
    invalidMetricsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    invalidMetricsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    invalidMetricsTable_->horizontalHeader()->setStretchLastSection(true);
    invalidMetricsModel_ = new HdfMetricsModel(invalidMetricsTable_);
    invalidMetricsModel_->setSource(&invalidFrames_);
    invalidMetricsTable_->setModel(invalidMetricsModel_);
    
    invalidLayout->addWidget(invalidImageScroll_, 1);
    invalidLayout->addWidget(invalidMetricsTable_, 1);
    
    frameTypeTabs_->addTab(invalidFramesWidget_, tr("Invalid Frames"));

    rootLayout->addWidget(frameTypeTabs_, 1);

    connect(frameTypeTabs_, QOverload<int>::of(&QTabWidget::currentChanged), 
            this, &HdfReviewTab::onTabChanged);
    connect(validMetricsTable_->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &HdfReviewTab::onTableSelectionChanged);
    connect(invalidMetricsTable_->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &HdfReviewTab::onTableSelectionChanged);
    connect(validMetricsTable_, &QTableView::doubleClicked,
            this, [this](const QModelIndex& idx) {
                if (idx.isValid()) onViewFrameDetails(idx.row());
            });
    connect(invalidMetricsTable_, &QTableView::doubleClicked,
            this, [this](const QModelIndex& idx) {
                if (idx.isValid()) onViewFrameDetails(idx.row());
            });
}

HdfReviewTab::~HdfReviewTab() = default;

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

void HdfReviewTab::loadHdfFile(const QString& filePath) {
    statusLabel_->setText(tr("Loading..."));
    filePathLabel_->setText(filePath);
    clearDisplay();

    SPDLOG_INFO("HdfReviewTab: opening file '{}'", filePath.toStdString());
    // Open and retain HDF5 file for the lifetime of this review session
    hdfReader_.reset();
    hdfReader_ = std::make_unique<backend::services::Hdf5Service>();
    if (!hdfReader_->loadFile(filePath.toStdString())) {
        QMessageBox::critical(this, tr("Error"), 
                             tr("Failed to open HDF file:\n%1").arg(filePath));
        statusLabel_->setText(tr("Error loading file"));
        return;
    }

    // Read experiment info and ROI
    uint64_t startTimeNs = 0, endTimeNs = 0;
    size_t totalValid = 0, totalInvalid = 0;
    backend::services::ProcessingService::Roi loadedRoi{0, 0, 0, 0};
    if (hdfReader_->readExperimentInfo(startTimeNs, endTimeNs, totalValid, totalInvalid, &loadedRoi)) {
        statusLabel_->setText(QString("Valid: %1, Invalid: %2")
                             .arg(totalValid).arg(totalInvalid));
        roi_ = loadedRoi;
        SPDLOG_INFO("Loaded ROI from HDF5: x={}, y={}, w={}, h={}", roi_.x, roi_.y, roi_.w, roi_.h);
        // Enable overlay checkbox if we have frames (for processing overlay) or valid ROI
        roiOverlayCheck_->setEnabled(true);
    } else {
        roi_ = {0, 0, 0, 0};
        SPDLOG_WARN("Failed to read experiment info or ROI not found in HDF5 file");
        // Still enable overlay checkbox if we have frames (for processing overlay)
        roiOverlayCheck_->setEnabled(false);
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
    exportMetricsBtn_->setEnabled(hasData);
    exportAllBtn_->setEnabled(hasData);
    exportChartsBtn_->setEnabled(hasData);
    
    // Enable overlay checkbox if we have frames (for processing overlay)
    if (hasData) {
        roiOverlayCheck_->setEnabled(true);
    }

    // Prefer actual dataset/metadata counts for status display (experiment info may be stale)
    {
        const size_t shownValid = !validFrames_.empty() ? validFrames_.size()
                                 : (validImagesCount > 0 ? validImagesCount : totalValid);
        const size_t shownInvalid = !invalidFrames_.empty() ? invalidFrames_.size()
                                   : (invalidImagesCount > 0 ? invalidImagesCount : totalInvalid);
        statusLabel_->setText(QString("Valid: %1, Invalid: %2")
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
    exportMetricsBtn_->setEnabled(false);
    exportAllBtn_->setEnabled(false);
    exportChartsBtn_->setEnabled(false);
    roiOverlayCheck_->setEnabled(false);
    roiOverlayCheck_->setChecked(false);

    // Clear valid frames grid
    QLayoutItem* item;
    while ((item = validImageGrid_->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
    // After clearing, the spacer pointer may be dangling; reset it
    validBottomSpacer_ = nullptr;
    validTopSpacer_ = nullptr;

    // Clear invalid frames grid
    while ((item = invalidImageGrid_->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
    // After clearing, the spacer pointer may be dangling; reset it
    invalidBottomSpacer_ = nullptr;
    invalidTopSpacer_ = nullptr;

    if (validMetricsModel_) validMetricsModel_->setSource(&validFrames_);
    if (invalidMetricsModel_) invalidMetricsModel_->setSource(&invalidFrames_);
}

void HdfReviewTab::updateImageGrid(const std::vector<backend::services::ProcessedFrame>& frames) {
    // Determine which grid to use based on which frames vector we're updating
    bool isValid = (&frames == &validFrames_);
    QGridLayout* grid = isValid ? validImageGrid_ : invalidImageGrid_;
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
            validImageGrid_->removeItem(validBottomSpacer_);
            delete validBottomSpacer_;
        }
        validBottomSpacer_ = new QSpacerItem(0, spacerH, QSizePolicy::Minimum, QSizePolicy::Fixed);
        validImageGrid_->addItem(validBottomSpacer_, static_cast<int>(loadedRows), 0, 1, GRID_COLUMNS);
    } else {
        if (invalidBottomSpacer_) {
            invalidImageGrid_->removeItem(invalidBottomSpacer_);
            delete invalidBottomSpacer_;
        }
        invalidBottomSpacer_ = new QSpacerItem(0, spacerH, QSizePolicy::Minimum, QSizePolicy::Fixed);
        invalidImageGrid_->addItem(invalidBottomSpacer_, static_cast<int>(loadedRows), 0, 1, GRID_COLUMNS);
    }
}

void HdfReviewTab::loadThumbnailsBatch(const std::vector<backend::services::ProcessedFrame>& frames,
                                        size_t startIndex, size_t count, bool isValid) {
    QGridLayout* grid = isValid ? validImageGrid_ : invalidImageGrid_;
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

            // Optional processing overlay if ROI/checkbox is enabled and mask exists
            if (showRoiOverlay_) {
                cv::Mat mask;
                if (hdfReader_->readImageByIndex(maskPath, i, mask) && !mask.empty()) {
                    thumbImage = createProcessingOverlay(original, mask);
                } else {
                    SPDLOG_DEBUG("HdfReviewTab: mask not available for {}[{}] (overlay on)", maskPath, i);
                    thumbImage = matToQImage(original);
                }

                // ROI rectangle overlay
                if (!thumbImage.isNull() && roi_.w > 0 && roi_.h > 0) {
                    thumbImage = drawRoiOverlay(thumbImage, original.cols, original.rows);
                }
            } else {
                thumbImage = matToQImage(original);
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
            validImageGrid_->removeItem(validBottomSpacer_);
            delete validBottomSpacer_;
        }
        validBottomSpacer_ = new QSpacerItem(0, spacerH, QSizePolicy::Minimum, QSizePolicy::Fixed);
        validImageGrid_->addItem(validBottomSpacer_, static_cast<int>(loadedRows), 0, 1, GRID_COLUMNS);
    } else {
        if (invalidBottomSpacer_) {
            invalidImageGrid_->removeItem(invalidBottomSpacer_);
            delete invalidBottomSpacer_;
        }
        invalidBottomSpacer_ = new QSpacerItem(0, spacerH, QSizePolicy::Minimum, QSizePolicy::Fixed);
        invalidImageGrid_->addItem(invalidBottomSpacer_, static_cast<int>(loadedRows), 0, 1, GRID_COLUMNS);
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
    QScrollArea* scrollArea = isShowingValid_ ? validImageScroll_ : invalidImageScroll_;
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
            validMetricsTable_->resizeColumnsToContents();
        }
    } else {
        if (invalidMetricsModel_) {
            invalidMetricsModel_->setSource(&invalidFrames_);
            invalidMetricsTable_->resizeColumnsToContents();
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
        QScrollArea* prevScroll = isShowingValid_ ? validImageScroll_ : invalidImageScroll_;
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
        QScrollArea* currScroll = isShowingValid_ ? validImageScroll_ : invalidImageScroll_;
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

void HdfReviewTab::onTableSelectionChanged() {
    QTableView* table = isShowingValid_ ? validMetricsTable_ : invalidMetricsTable_;
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
    QGridLayout* grid = isShowingValid_ ? validImageGrid_ : invalidImageGrid_;
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
    QTableView* table = isShowingValid_ ? validMetricsTable_ : invalidMetricsTable_;
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
    
    // CSV header
    out << "Frame Type,Index,Timestamp,Deformability,Area,Area Ratio,Ring Ratio,"
        << "Valid,Touches Border,Single Inner,In Range,Inner Count,"
        << "Bright Q1,Bright Q2,Bright Q3,Bright Q4\n";

    // Export valid frames
    for (const auto& frame : validFrames_) {
        const auto& val = frame.validation;
        out << "Valid,";
        out << frame.index << ",";
        out << frame.timestampNs << ",";
        out << QString::number(val.deformability, 'f', 3) << ",";
        out << QString::number(val.area, 'f', 2) << ",";
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
        out << "Invalid,";
        out << frame.index << ",";
        out << frame.timestampNs << ",";
        out << QString::number(val.deformability, 'f', 3) << ",";
        out << QString::number(val.area, 'f', 2) << ",";
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

void HdfReviewTab::onToggleRoiOverlay(bool enabled) {
    showRoiOverlay_ = enabled;
    SPDLOG_INFO("ROI overlay toggled: {}, ROI: x={}, y={}, w={}, h={}", 
                enabled, roi_.x, roi_.y, roi_.w, roi_.h);
    // Clear thumbnail cache so images get rebuilt with new overlay state
    thumbnailCache_.clear();

    // Preserve current scroll positions
    if (validImageScroll_ && validImageScroll_->verticalScrollBar()) {
        validScrollValue_ = validImageScroll_->verticalScrollBar()->value();
    }
    if (invalidImageScroll_ && invalidImageScroll_->verticalScrollBar()) {
        invalidScrollValue_ = invalidImageScroll_->verticalScrollBar()->value();
    }

    // Refresh only what is visible in each tab (carousel-like behavior)
    refreshVisibleThumbnails(true);
    refreshVisibleThumbnails(false);
    pruneOffscreenThumbnails(true);
    pruneOffscreenThumbnails(false);

    // Restore scroll positions
    if (validImageScroll_ && validImageScroll_->verticalScrollBar()) {
        validImageScroll_->verticalScrollBar()->setValue(validScrollValue_);
    }
    if (invalidImageScroll_ && invalidImageScroll_->verticalScrollBar()) {
        invalidImageScroll_->verticalScrollBar()->setValue(invalidScrollValue_);
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

QImage HdfReviewTab::createProcessingOverlay(const cv::Mat& original, const cv::Mat& mask) const {
    if (original.empty() || mask.empty()) {
        return matToQImage(original);
    }
    
    // Convert original to RGB if needed
    cv::Mat rgb;
    if (original.channels() == 1) {
        cv::cvtColor(original, rgb, cv::COLOR_GRAY2RGB);
    } else {
        rgb = original.clone();
        if (rgb.channels() == 3) {
            cv::cvtColor(rgb, rgb, cv::COLOR_BGR2RGB);
        }
    }
    
    // Create overlay: green tint where mask is non-zero
    cv::Mat overlay = rgb.clone();
    for (int y = 0; y < overlay.rows && y < mask.rows; ++y) {
        for (int x = 0; x < overlay.cols && x < mask.cols; ++x) {
            if (mask.at<uchar>(y, x) > 0) {
                cv::Vec3b& pixel = overlay.at<cv::Vec3b>(y, x);
                // Blend with green (0, 255, 0) at 30% opacity
                pixel[0] = static_cast<uchar>(pixel[0] * 0.7); // R
                pixel[1] = static_cast<uchar>(std::min(255.0, pixel[1] * 0.7 + 255.0 * 0.3)); // G
                pixel[2] = static_cast<uchar>(pixel[2] * 0.7); // B
            }
        }
    }
    
    QImage img(overlay.data, overlay.cols, overlay.rows, static_cast<int>(overlay.step), QImage::Format_RGB888);
    return img.copy();
}

void HdfReviewTab::computeVisibleRange(bool isValid, size_t &outStartIndex, size_t &outEndIndex) const {
    const auto& frames = isValid ? validFrames_ : invalidFrames_;
    outStartIndex = 0;
    outEndIndex = 0;
    if (frames.empty()) return;

    const QScrollArea* scrollArea = isValid ? validImageScroll_ : invalidImageScroll_;
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

    if (showRoiOverlay_) {
        cv::Mat mask;
        if (hdfReader_->readImageByIndex(maskPath, index, mask) && !mask.empty()) {
            thumbImage = createProcessingOverlay(original, mask);
        } else {
            thumbImage = matToQImage(original);
        }
        if (!thumbImage.isNull() && roi_.w > 0 && roi_.h > 0) {
            thumbImage = drawRoiOverlay(thumbImage, original.cols, original.rows);
        }
    } else {
        thumbImage = matToQImage(original);
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

    QGridLayout* grid = isValid ? validImageGrid_ : invalidImageGrid_;

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
            validImageGrid_->removeItem(validTopSpacer_);
            delete validTopSpacer_;
        }
        validTopSpacer_ = new QSpacerItem(0, static_cast<int>(startRow) * cellH, QSizePolicy::Minimum, QSizePolicy::Fixed);
        validImageGrid_->addItem(validTopSpacer_, 0, 0, 1, GRID_COLUMNS);
    } else {
        if (invalidTopSpacer_) {
            invalidImageGrid_->removeItem(invalidTopSpacer_);
            delete invalidTopSpacer_;
        }
        invalidTopSpacer_ = new QSpacerItem(0, static_cast<int>(startRow) * cellH, QSizePolicy::Minimum, QSizePolicy::Fixed);
        invalidImageGrid_->addItem(invalidTopSpacer_, 0, 0, 1, GRID_COLUMNS);
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
            validImageGrid_->removeItem(validBottomSpacer_);
            delete validBottomSpacer_;
        }
        validBottomSpacer_ = new QSpacerItem(0, bottomH, QSizePolicy::Minimum, QSizePolicy::Fixed);
        validImageGrid_->addItem(validBottomSpacer_, localRowBase + static_cast<int>(visibleRows), 0, 1, GRID_COLUMNS);
    } else {
        if (invalidBottomSpacer_) {
            invalidImageGrid_->removeItem(invalidBottomSpacer_);
            delete invalidBottomSpacer_;
        }
        invalidBottomSpacer_ = new QSpacerItem(0, bottomH, QSizePolicy::Minimum, QSizePolicy::Fixed);
        invalidImageGrid_->addItem(invalidBottomSpacer_, localRowBase + static_cast<int>(visibleRows), 0, 1, GRID_COLUMNS);
    }
}

void HdfReviewTab::pruneOffscreenThumbnails(bool isValid) {
    const auto& frames = isValid ? validFrames_ : invalidFrames_;
    if (frames.empty()) return;

    size_t keepStart = 0, keepEnd = 0;
    computeVisibleRange(isValid, keepStart, keepEnd);
    if (keepEnd <= keepStart) return;

    QGridLayout* grid = isValid ? validImageGrid_ : invalidImageGrid_;

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
            validImageGrid_->removeItem(validBottomSpacer_);
            delete validBottomSpacer_;
        }
        validBottomSpacer_ = new QSpacerItem(0, spacerH, QSizePolicy::Minimum, QSizePolicy::Fixed);
        validImageGrid_->addItem(validBottomSpacer_, static_cast<int>(loadedRows), 0, 1, GRID_COLUMNS);
    } else {
        if (invalidBottomSpacer_) {
            invalidImageGrid_->removeItem(invalidBottomSpacer_);
            delete invalidBottomSpacer_;
        }
        invalidBottomSpacer_ = new QSpacerItem(0, spacerH, QSizePolicy::Minimum, QSizePolicy::Fixed);
        invalidImageGrid_->addItem(invalidBottomSpacer_, static_cast<int>(loadedRows), 0, 1, GRID_COLUMNS);
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
    }
    
    // Create dialog
    auto* dialog = new FrameViewerDialog(initialFrame, roi_, showRoiOverlay_, this);
    
    // Store current index in a way that can be modified by lambdas
    struct NavigationState {
        int currentIndex;
        const std::vector<backend::services::ProcessedFrame>* framesPtr;
        bool isValidSet;
    };
    
    auto* navState = new NavigationState{frameIndex, &framesMeta, isShowingValid_};
    
    // Connect navigation signals
    connect(dialog, &FrameViewerDialog::requestPreviousFrame, this, [this, dialog, navState]() {
        navState->currentIndex = navState->currentIndex - 1;
        if (navState->currentIndex < 0) {
            navState->currentIndex = static_cast<int>(navState->framesPtr->size()) - 1; // Wrap to last
        }
        if (navState->currentIndex >= 0 && navState->currentIndex < static_cast<int>(navState->framesPtr->size())) {
            // Fetch images on demand
            backend::services::ProcessedFrame pf = (*navState->framesPtr)[navState->currentIndex];
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
            dialog->setFrame(pf);
            // Update selected frame in main view
            setSelectedFrame(navState->currentIndex);
        }
    });
    
    connect(dialog, &FrameViewerDialog::requestNextFrame, this, [this, dialog, navState]() {
        navState->currentIndex = navState->currentIndex + 1;
        if (navState->currentIndex >= static_cast<int>(navState->framesPtr->size())) {
            navState->currentIndex = 0; // Wrap to first
        }
        if (navState->currentIndex >= 0 && navState->currentIndex < static_cast<int>(navState->framesPtr->size())) {
            backend::services::ProcessedFrame pf = (*navState->framesPtr)[navState->currentIndex];
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
    if (!hdfReader_) {
        QMessageBox::warning(this, tr("Export Error"),
                            tr("No HDF5 file loaded."));
        return;
    }

    QString dirPath = QFileDialog::getExistingDirectory(this, tr("Select Directory to Export Charts"),
                                                        "", QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dirPath.isEmpty()) {
        return;
    }

    QDir dir(dirPath);
    bool success = true;
    
    // Export histogram
    QString histogramPath = dir.filePath("ring_width_histogram.tiff");
    if (!exportChartFromHdf5("/experiment_info/chart_snapshots/ring_width_histogram", histogramPath)) {
        success = false;
    }
    
    // Export scatter plot
    QString scatterPath = dir.filePath("scatter_plot.tiff");
    if (!exportChartFromHdf5("/experiment_info/chart_snapshots/scatter_plot", scatterPath)) {
        success = false;
    }
    
    if (success) {
        QMessageBox::information(this, tr("Export Complete"),
                                tr("Charts exported successfully to:\n%1").arg(dirPath));
    } else {
        QMessageBox::warning(this, tr("Export Warning"),
                            tr("Some charts may not have been exported. Chart snapshots may not exist in HDF5 file."));
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
    int totalCount = static_cast<int>(validFrames_.size() + invalidFrames_.size());

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

    QMessageBox::information(this, tr("Export Complete"),
                            tr("Exported %1 of %2 images to:\n%3")
                            .arg(exportedCount)
                            .arg(totalCount)
                            .arg(baseDir));
    SPDLOG_INFO("Exported {} of {} images to {}", exportedCount, totalCount, baseDir.toStdString());
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

    // Export charts
    QString histogramPath = dir.filePath("ring_width_histogram.tiff");
    if (!exportChartFromHdf5("/experiment_info/chart_snapshots/ring_width_histogram", histogramPath)) {
        chartsExported = false;
    }
    
    QString scatterPath = dir.filePath("scatter_plot.tiff");
    if (!exportChartFromHdf5("/experiment_info/chart_snapshots/scatter_plot", scatterPath)) {
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

} // namespace frontend

// Include moc file for ThumbnailLabel class (defined in this .cpp file with Q_OBJECT)
#include "HdfReviewTab.moc"


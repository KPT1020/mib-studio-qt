#include "frontend/HdfReviewTab.h"

#include <QPushButton>
#include <QLabel>
#include <QTabWidget>
#include <QTableWidget>
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
#include <QFile>
#include <QTextStream>
#include <QCheckBox>
#include <algorithm>

#include "backend/AppBackend.h"
#include "backend/services/Hdf5Service.h"
#include "backend/services/ProcessingService.h"

#include <spdlog/spdlog.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

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

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            emit clicked(frameIndex_);
        }
        QLabel::mousePressEvent(event);
    }

private:
    int frameIndex_;
};

HdfReviewTab::HdfReviewTab(backend::AppBackend& backend, QWidget* parent)
    : QWidget(parent), backend_(backend) {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(6, 6, 6, 6);
    rootLayout->setSpacing(6);

    // File selection row
    auto* fileRow = new QHBoxLayout();
    selectFileBtn_ = new QPushButton(tr("Select HDF File..."), this);
    exportMetricsBtn_ = new QPushButton(tr("Export Metrics to CSV..."), this);
    exportMetricsBtn_->setEnabled(false);
    roiOverlayCheck_ = new QCheckBox(tr("Show Overlays"), this);
    roiOverlayCheck_->setEnabled(false);
    filePathLabel_ = new QLabel(tr("No file selected"), this);
    statusLabel_ = new QLabel(tr("Ready"), this);
    fileRow->addWidget(selectFileBtn_);
    fileRow->addWidget(exportMetricsBtn_);
    fileRow->addWidget(roiOverlayCheck_);
    fileRow->addWidget(filePathLabel_, 1);
    fileRow->addWidget(statusLabel_);
    rootLayout->addLayout(fileRow);

    connect(selectFileBtn_, &QPushButton::clicked, this, &HdfReviewTab::onSelectFile);
    connect(exportMetricsBtn_, &QPushButton::clicked, this, &HdfReviewTab::onExportMetrics);
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
    connect(validImageScroll_->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &HdfReviewTab::onScrollValueChanged);
    
    validMetricsTable_ = new QTableWidget(validFramesWidget_);
    validMetricsTable_->setColumnCount(15);
    QStringList headers = {
        "Index", "Timestamp", "Deformability", "Area", "Area Ratio", "Ring Ratio",
        "Valid", "Touches Border", "Single Inner", "In Range", "Inner Count",
        "Bright Q1", "Bright Q2", "Bright Q3", "Bright Q4"
    };
    validMetricsTable_->setHorizontalHeaderLabels(headers);
    validMetricsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    validMetricsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    validMetricsTable_->setSortingEnabled(true);
    validMetricsTable_->horizontalHeader()->setStretchLastSection(true);
    
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
    connect(invalidImageScroll_->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &HdfReviewTab::onScrollValueChanged);
    
    invalidMetricsTable_ = new QTableWidget(invalidFramesWidget_);
    invalidMetricsTable_->setColumnCount(15);
    invalidMetricsTable_->setHorizontalHeaderLabels(headers);
    invalidMetricsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    invalidMetricsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    invalidMetricsTable_->setSortingEnabled(true);
    invalidMetricsTable_->horizontalHeader()->setStretchLastSection(true);
    
    invalidLayout->addWidget(invalidImageScroll_, 1);
    invalidLayout->addWidget(invalidMetricsTable_, 1);
    
    frameTypeTabs_->addTab(invalidFramesWidget_, tr("Invalid Frames"));

    rootLayout->addWidget(frameTypeTabs_, 1);

    connect(frameTypeTabs_, QOverload<int>::of(&QTabWidget::currentChanged), 
            this, &HdfReviewTab::onTabChanged);
    connect(validMetricsTable_, &QTableWidget::itemSelectionChanged,
            this, &HdfReviewTab::onTableSelectionChanged);
    connect(invalidMetricsTable_, &QTableWidget::itemSelectionChanged,
            this, &HdfReviewTab::onTableSelectionChanged);
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

    // Use a temporary Hdf5Service instance for reading
    backend::services::Hdf5Service hdf5Service;
    if (!hdf5Service.loadFile(filePath.toStdString())) {
        QMessageBox::critical(this, tr("Error"), 
                             tr("Failed to open HDF file:\n%1").arg(filePath));
        statusLabel_->setText(tr("Error loading file"));
        return;
    }

    // Read experiment info and ROI
    uint64_t startTimeNs = 0, endTimeNs = 0;
    size_t totalValid = 0, totalInvalid = 0;
    backend::services::ProcessingService::Roi loadedRoi{0, 0, 0, 0};
    if (hdf5Service.readExperimentInfo(startTimeNs, endTimeNs, totalValid, totalInvalid, &loadedRoi)) {
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

    // Read frames
    if (!hdf5Service.readValidFrames(validFrames_)) {
        SPDLOG_WARN("Failed to read valid frames or no valid frames found");
        validFrames_.clear();
    }

    if (!hdf5Service.readInvalidFrames(invalidFrames_)) {
        SPDLOG_WARN("Failed to read invalid frames or no invalid frames found");
        invalidFrames_.clear();
    }

    hdf5Service.closeFile();

    // Populate UI
    updateImageGrid(validFrames_);
    updateMetricsTable(validFrames_);
    updateImageGrid(invalidFrames_);
    updateMetricsTable(invalidFrames_);

    // Enable export button if we have any data
    exportMetricsBtn_->setEnabled(!validFrames_.empty() || !invalidFrames_.empty());
    
    // Enable overlay checkbox if we have frames (for processing overlay)
    if (!validFrames_.empty() || !invalidFrames_.empty()) {
        roiOverlayCheck_->setEnabled(true);
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
    
    // Disable export button and ROI overlay when no data
    exportMetricsBtn_->setEnabled(false);
    roiOverlayCheck_->setEnabled(false);
    roiOverlayCheck_->setChecked(false);

    // Clear valid frames grid
    QLayoutItem* item;
    while ((item = validImageGrid_->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }

    // Clear invalid frames grid
    while ((item = invalidImageGrid_->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }

    validMetricsTable_->setRowCount(0);
    invalidMetricsTable_->setRowCount(0);
}

void HdfReviewTab::updateImageGrid(const std::vector<backend::services::ProcessedFrame>& frames) {
    // Determine which grid to use based on which frames vector we're updating
    bool isValid = (&frames == &validFrames_);
    QGridLayout* grid = isValid ? validImageGrid_ : invalidImageGrid_;
    
    // Clear existing thumbnails
    QLayoutItem* item;
    while ((item = grid->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
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

    // Add placeholder labels for remaining frames if any
    if (frames.size() > initialCount) {
        size_t totalRows = (frames.size() + GRID_COLUMNS - 1) / GRID_COLUMNS;
        size_t loadedRows = (initialCount + GRID_COLUMNS - 1) / GRID_COLUMNS;
        
        for (size_t row = loadedRows; row < totalRows; ++row) {
            for (int col = 0; col < GRID_COLUMNS; ++col) {
                size_t frameIndex = row * GRID_COLUMNS + col;
                if (frameIndex >= frames.size()) {
                    break;
                }
                // Create placeholder label
                auto* placeholder = new QLabel(grid->parentWidget());
                placeholder->setMinimumSize(THUMBNAIL_SIZE, THUMBNAIL_SIZE);
                placeholder->setMaximumSize(THUMBNAIL_SIZE, THUMBNAIL_SIZE);
                placeholder->setStyleSheet("QLabel { border: 1px solid gray; background-color: #f0f0f0; }");
                placeholder->setAlignment(Qt::AlignCenter);
                placeholder->setText(QString("Frame %1\n(Click to load)").arg(frameIndex));
                grid->addWidget(placeholder, static_cast<int>(row), col);
            }
        }
    }
}

void HdfReviewTab::loadThumbnailsBatch(const std::vector<backend::services::ProcessedFrame>& frames,
                                        size_t startIndex, size_t count, bool isValid) {
    QGridLayout* grid = isValid ? validImageGrid_ : invalidImageGrid_;
    size_t endIndex = std::min(startIndex + count, frames.size());
    
    for (size_t i = startIndex; i < endIndex; ++i) {
        const auto& frame = frames[i];
        QImage thumbnail;
        
        // Apply processing mask overlay if enabled
        if (showRoiOverlay_ && !frame.processedImage.empty() && !frame.originalImage.empty()) {
            thumbnail = createProcessingOverlay(frame.originalImage, frame.processedImage);
        } else {
            thumbnail = matToQImage(frame.originalImage);
        }
        
        // Apply ROI rectangle overlay if enabled
        if (showRoiOverlay_ && roi_.w > 0 && roi_.h > 0 && !thumbnail.isNull()) {
            thumbnail = drawRoiOverlay(thumbnail, frame.originalImage.cols, frame.originalImage.rows);
        }
        
        // Scale to thumbnail size
        QImage scaled = thumbnail.scaled(THUMBNAIL_SIZE, THUMBNAIL_SIZE, 
                                         Qt::KeepAspectRatio, Qt::SmoothTransformation);
        
        auto* label = new ThumbnailLabel(static_cast<int>(i), THUMBNAIL_SIZE, grid->parentWidget());
        label->setPixmap(QPixmap::fromImage(scaled));
        label->setToolTip(QString("Frame %1\nIndex: %2").arg(i).arg(frame.index));
        
        connect(label, &ThumbnailLabel::clicked, this, &HdfReviewTab::onThumbnailClicked);
        
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
    }
}

void HdfReviewTab::onScrollValueChanged(int value) {
    QScrollArea* scrollArea = isShowingValid_ ? validImageScroll_ : invalidImageScroll_;
    const auto& frames = isShowingValid_ ? validFrames_ : invalidFrames_;
    size_t& loadedCount = isShowingValid_ ? validThumbnailsLoaded_ : invalidThumbnailsLoaded_;
    
    if (frames.empty() || loadedCount >= frames.size()) {
        return;
    }
    
    // Check if user scrolled near the bottom (within 2 rows of loaded content)
    QScrollBar* scrollBar = scrollArea->verticalScrollBar();
    int maxValue = scrollBar->maximum();
    int threshold = maxValue - (THUMBNAIL_SIZE * 2); // 2 rows worth
    
    if (value >= threshold && loadedCount < frames.size()) {
        // Load next batch
        size_t batchSize = std::min(BATCH_THUMBNAIL_COUNT, frames.size() - loadedCount);
        loadThumbnailsBatch(frames, loadedCount, batchSize, isShowingValid_);
        loadedCount += batchSize;
        
        SPDLOG_DEBUG("Loaded thumbnail batch: {} total loaded out of {}", loadedCount, frames.size());
    }
}

void HdfReviewTab::updateMetricsTable(const std::vector<backend::services::ProcessedFrame>& frames) {
    // Determine which table to use based on which frames vector we're updating
    bool isValid = (&frames == &validFrames_);
    QTableWidget* table = isValid ? validMetricsTable_ : invalidMetricsTable_;
    table->setRowCount(static_cast<int>(frames.size()));

    for (size_t i = 0; i < frames.size(); ++i) {
        const auto& frame = frames[i];
        const auto& val = frame.validation;

        int row = static_cast<int>(i);
        table->setItem(row, 0, new QTableWidgetItem(QString::number(frame.index)));
        table->setItem(row, 1, new QTableWidgetItem(QString::number(frame.timestampNs)));
        table->setItem(row, 2, new QTableWidgetItem(QString::number(val.deformability, 'f', 3)));
        table->setItem(row, 3, new QTableWidgetItem(QString::number(val.area, 'f', 2)));
        table->setItem(row, 4, new QTableWidgetItem(QString::number(val.areaRatio, 'f', 3)));
        table->setItem(row, 5, new QTableWidgetItem(QString::number(val.ringRatio, 'f', 3)));
        table->setItem(row, 6, new QTableWidgetItem(val.isValid ? "Yes" : "No"));
        table->setItem(row, 7, new QTableWidgetItem(val.touchesBorder ? "Yes" : "No"));
        table->setItem(row, 8, new QTableWidgetItem(val.hasSingleInnerContour ? "Yes" : "No"));
        table->setItem(row, 9, new QTableWidgetItem(val.inRange ? "Yes" : "No"));
        table->setItem(row, 10, new QTableWidgetItem(QString::number(val.innerContourCount)));
        table->setItem(row, 11, new QTableWidgetItem(QString::number(val.brightness.q1, 'f', 2)));
        table->setItem(row, 12, new QTableWidgetItem(QString::number(val.brightness.q2, 'f', 2)));
        table->setItem(row, 13, new QTableWidgetItem(QString::number(val.brightness.q3, 'f', 2)));
        table->setItem(row, 14, new QTableWidgetItem(QString::number(val.brightness.q4, 'f', 2)));
    }

    table->resizeColumnsToContents();
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
    isShowingValid_ = (index == 0);
    if (isShowingValid_) {
        updateImageGrid(validFrames_);
        updateMetricsTable(validFrames_);
    } else {
        updateImageGrid(invalidFrames_);
        updateMetricsTable(invalidFrames_);
    }
    selectedFrameIndex_ = -1;
}

void HdfReviewTab::onThumbnailClicked(int frameIndex) {
    setSelectedFrame(frameIndex);
}

void HdfReviewTab::onTableSelectionChanged() {
    QTableWidget* table = isShowingValid_ ? validMetricsTable_ : invalidMetricsTable_;
    QList<QTableWidgetItem*> selected = table->selectedItems();
    if (!selected.isEmpty()) {
        int row = selected[0]->row();
        setSelectedFrame(row);
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
    QTableWidget* table = isShowingValid_ ? validMetricsTable_ : invalidMetricsTable_;
    table->selectRow(frameIndex);
    table->scrollToItem(table->item(frameIndex, 0));
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
    // Refresh thumbnails to show/hide overlay for both valid and invalid frames
    updateImageGrid(validFrames_);
    updateImageGrid(invalidFrames_);
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

} // namespace frontend

// Include moc file for ThumbnailLabel class (defined in this .cpp file with Q_OBJECT)
#include "HdfReviewTab.moc"


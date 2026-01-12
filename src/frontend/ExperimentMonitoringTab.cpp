#include "frontend/ExperimentMonitoringTab.h"

#include <QTimer>
#include <QGridLayout>
#include <QScrollArea>
#include <QLabel>
#include <QPixmap>
#include <QPainter>
#include <QChartView>
#include <QScatterSeries>
#include <QBarSeries>
#include <QBarSet>
#include <QChart>
#include <QValueAxis>
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
#include <QBarCategoryAxis>
#endif
#include <QFrame>
#include <QCheckBox>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QShowEvent>
#include <QHideEvent>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <numeric>

#include "backend/AppBackend.h"
#include "backend/services/ProcessingService.h"

#include <spdlog/spdlog.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace frontend
{

    ExperimentMonitoringTab::ExperimentMonitoringTab(backend::AppBackend &backend, QWidget *parent)
        : QWidget(parent), backend_(backend)
    {
        auto *rootLayout = new QGridLayout(this);
        rootLayout->setContentsMargins(6, 6, 6, 6);
        rootLayout->setSpacing(6);

        // Panel 1: Scatterplot (top-left)
        scatterplotChart_ = new QChart();
        scatterSeries_ = new QScatterSeries();
        scatterSeries_->setMarkerSize(6.0);
        scatterSeries_->setName("Valid Frames");
        scatterplotChart_->addSeries(scatterSeries_);
        scatterplotChart_->setTitle("Deformability vs Area");
        scatterplotChart_->legend()->setVisible(false);

        scatterXAxis_ = new QValueAxis();
        scatterXAxis_->setTitleText("Area");
        scatterYAxis_ = new QValueAxis();
        scatterYAxis_->setTitleText("Deformability");
        scatterplotChart_->addAxis(scatterXAxis_, Qt::AlignBottom);
        scatterplotChart_->addAxis(scatterYAxis_, Qt::AlignLeft);
        scatterSeries_->attachAxis(scatterXAxis_);
        scatterSeries_->attachAxis(scatterYAxis_);

        scatterplotView_ = new QChartView(scatterplotChart_);
        scatterplotView_->setRenderHint(QPainter::Antialiasing);
        rootLayout->addWidget(scatterplotView_, 1, 0);

        // Panel 2: Histogram (top-right)
        histogramChart_ = new QChart();
        histogramChart_->setTitle("Ring Width Distribution");
        histogramChart_->legend()->setVisible(false);

        histogramYAxis_ = new QValueAxis();
        histogramYAxis_->setTitleText("Frequency");
        histogramChart_->addAxis(histogramYAxis_, Qt::AlignLeft);

        histogramXAxis_ = new QValueAxis();
        histogramXAxis_->setLabelsAngle(-90);
        histogramXAxis_->setLabelFormat("%.2f");
        histogramChart_->addAxis(histogramXAxis_, Qt::AlignBottom);

#if MIB_HAS_QHISTOGRAMSERIES
        histogramSeries_ = new QHistogramSeries();
        histogramSeries_->setName("Ring Width");
        histogramChart_->addSeries(histogramSeries_);
        histogramSeries_->attachAxis(histogramXAxis_);
        histogramSeries_->attachAxis(histogramYAxis_);
#else
        barSeries_ = new QBarSeries();
        histogramChart_->addSeries(barSeries_);
        // Fallback: use category axis for X when bar series is used
        histogramCategoryAxis_ = new QBarCategoryAxis();
        histogramCategoryAxis_->setLabelsAngle(-90);
        // Remove previously added numeric X axis to avoid overlap
        if (histogramXAxis_)
        {
            histogramChart_->removeAxis(histogramXAxis_);
            delete histogramXAxis_;
            histogramXAxis_ = nullptr;
        }
        histogramChart_->addAxis(histogramCategoryAxis_, Qt::AlignBottom);
        barSeries_->attachAxis(histogramCategoryAxis_);
        barSeries_->attachAxis(histogramYAxis_);
#endif

        histogramView_ = new QChartView(histogramChart_);
        histogramView_->setRenderHint(QPainter::Antialiasing);
        rootLayout->addWidget(histogramView_, 1, 1);

        // Panel 3: Valid frames grid (bottom-left)
        validFramesContainer_ = new QWidget(this);
        validFramesLayout_ = new QVBoxLayout(validFramesContainer_);
        validFramesLayout_->setContentsMargins(0, 0, 0, 0);
        validFramesLayout_->setSpacing(4);

        validFramesControls_ = new QHBoxLayout();
        validOverlayCheck_ = new QCheckBox(tr("Show Overlay"), validFramesContainer_);
        validOverlayCheck_->setChecked(false);
        connect(validOverlayCheck_, &QCheckBox::toggled, this, &ExperimentMonitoringTab::onToggleOverlay);
        validFramesControls_->addWidget(validOverlayCheck_);
        validFramesControls_->addStretch();
        validFramesLayout_->addLayout(validFramesControls_);

        validFramesScroll_ = new QScrollArea(validFramesContainer_);
        validFramesWidget_ = new QWidget();
        validFramesGrid_ = new QGridLayout(validFramesWidget_);
        validFramesGrid_->setSpacing(4);
        validFramesScroll_->setWidget(validFramesWidget_);
        validFramesScroll_->setWidgetResizable(true);
        validFramesScroll_->setMinimumHeight(200);
        validFramesLayout_->addWidget(validFramesScroll_);
        rootLayout->addWidget(validFramesContainer_, 2, 0);

        // Panel 4: Invalid frames grid (bottom-right)
        invalidFramesContainer_ = new QWidget(this);
        invalidFramesLayout_ = new QVBoxLayout(invalidFramesContainer_);
        invalidFramesLayout_->setContentsMargins(0, 0, 0, 0);
        invalidFramesLayout_->setSpacing(4);

        invalidFramesControls_ = new QHBoxLayout();
        invalidOverlayCheck_ = new QCheckBox(tr("Show Overlay"), invalidFramesContainer_);
        invalidOverlayCheck_->setChecked(false);
        connect(invalidOverlayCheck_, &QCheckBox::toggled, this, &ExperimentMonitoringTab::onToggleOverlay);
        invalidFramesControls_->addWidget(invalidOverlayCheck_);
        invalidFramesControls_->addStretch();
        invalidFramesLayout_->addLayout(invalidFramesControls_);

        invalidFramesScroll_ = new QScrollArea(invalidFramesContainer_);
        invalidFramesWidget_ = new QWidget();
        invalidFramesGrid_ = new QGridLayout(invalidFramesWidget_);
        invalidFramesGrid_->setSpacing(4);
        invalidFramesScroll_->setWidget(invalidFramesWidget_);
        invalidFramesScroll_->setWidgetResizable(true);
        invalidFramesScroll_->setMinimumHeight(200);
        invalidFramesLayout_->addWidget(invalidFramesScroll_);
        rootLayout->addWidget(invalidFramesContainer_, 2, 1);

        // Add clear buffer button in a separate row above charts
        auto *topRow = new QHBoxLayout();
        clearBufferBtn_ = new QPushButton(tr("Clear Buffer"), this);
        connect(clearBufferBtn_, &QPushButton::clicked, this, &ExperimentMonitoringTab::onClearBuffer);
        topRow->addWidget(clearBufferBtn_);
        topRow->addStretch();
        rootLayout->addLayout(topRow, 0, 0, 1, 2);

        // Set column stretch to make panels equal size
        rootLayout->setColumnStretch(0, 1);
        rootLayout->setColumnStretch(1, 1);
        rootLayout->setRowStretch(1, 1); // Charts row
        rootLayout->setRowStretch(2, 1); // Frame grids row

        // Setup update timer
        updateTimer_ = new QTimer(this);
        updateTimer_->setInterval(UPDATE_INTERVAL_MS);
        connect(updateTimer_, &QTimer::timeout, this, &ExperimentMonitoringTab::onUpdate);
    }

    void ExperimentMonitoringTab::onUpdate()
    {
        // Get frames from processing service (use monitoring frames which work without experiment)
        auto validFrames = backend_.processing().getMonitoringValidFrames();
        auto invalidFrames = backend_.processing().getMonitoringInvalidFrames();

        // Merge new frames into rolling buffers (maintain recent history)
        // Track seen frame indices to avoid duplicates
        std::set<uint64_t> seenValidIndices;
        std::set<uint64_t> seenInvalidIndices;
        for (const auto &frame : recentValidFrames_)
        {
            seenValidIndices.insert(frame.index);
        }
        for (const auto &frame : recentInvalidFrames_)
        {
            seenInvalidIndices.insert(frame.index);
        }

        // Add new frames that we haven't seen
        for (const auto &frame : validFrames)
        {
            if (seenValidIndices.find(frame.index) == seenValidIndices.end())
            {
                recentValidFrames_.push_back(frame);
                if (frame.index > lastValidFrameIndex_)
                {
                    lastValidFrameIndex_ = frame.index;
                }
            }
        }
        for (const auto &frame : invalidFrames)
        {
            if (seenInvalidIndices.find(frame.index) == seenInvalidIndices.end())
            {
                recentInvalidFrames_.push_back(frame);
                if (frame.index > lastInvalidFrameIndex_)
                {
                    lastInvalidFrameIndex_ = frame.index;
                }
            }
        }

        // Trim to max size, keeping most recent frames
        if (recentValidFrames_.size() > MAX_RECENT_FRAMES)
        {
            size_t excess = recentValidFrames_.size() - MAX_RECENT_FRAMES;
            recentValidFrames_.erase(recentValidFrames_.begin(), recentValidFrames_.begin() + excess);
        }
        if (recentInvalidFrames_.size() > MAX_RECENT_FRAMES)
        {
            size_t excess = recentInvalidFrames_.size() - MAX_RECENT_FRAMES;
            recentInvalidFrames_.erase(recentInvalidFrames_.begin(), recentInvalidFrames_.begin() + excess);
        }

        // Update all panels using rolling buffers
        updateScatterplot(recentValidFrames_);
        updateHistogram(recentValidFrames_);
        updateValidFramesGrid(recentValidFrames_);
        updateInvalidFramesGrid(recentInvalidFrames_);
    }

    void ExperimentMonitoringTab::showEvent(QShowEvent *event)
    {
        QWidget::showEvent(event);
        if (updateTimer_ && !updateTimer_->isActive())
        {
            updateTimer_->start();
        }
    }

    void ExperimentMonitoringTab::hideEvent(QHideEvent *event)
    {
        QWidget::hideEvent(event);
        if (updateTimer_ && updateTimer_->isActive())
        {
            updateTimer_->stop();
        }
    }

    void ExperimentMonitoringTab::updateScatterplot(const std::vector<backend::services::ProcessedFrame> &validFrames)
    {
        scatterSeries_->clear();

        if (validFrames.empty())
        {
            scatterXAxis_->setRange(0, 1000);
            scatterYAxis_->setRange(0, 1);
            return;
        }

        // Collect points
        std::vector<std::pair<double, double>> points;
        double minArea = std::numeric_limits<double>::max();
        double maxArea = std::numeric_limits<double>::lowest();
        double minDeform = std::numeric_limits<double>::max();
        double maxDeform = std::numeric_limits<double>::lowest();

        for (const auto &frame : validFrames)
        {
            if (frame.validation.isValid)
            {
                double area = frame.validation.area;
                double deform = frame.validation.deformability;
                points.push_back({area, deform});

                minArea = std::min(minArea, area);
                maxArea = std::max(maxArea, area);
                minDeform = std::min(minDeform, deform);
                maxDeform = std::max(maxDeform, deform);
            }
        }

        if (points.empty())
        {
            scatterXAxis_->setRange(0, 1000);
            scatterYAxis_->setRange(0, 1);
            return;
        }

        // Add the original scatter points (KDE disabled for performance)
        for (const auto &p : points)
        {
            scatterSeries_->append(p.first, p.second);
        }

        // Set axis ranges with padding
        if (minArea < maxArea)
        {
            double areaPadding = (maxArea - minArea) * 0.1;
            scatterXAxis_->setRange(minArea - areaPadding, maxArea + areaPadding);
        }
        else
        {
            scatterXAxis_->setRange(0, 1000);
        }

        if (minDeform < maxDeform)
        {
            double deformPadding = (maxDeform - minDeform) * 0.1;
            scatterYAxis_->setRange(minDeform - deformPadding, maxDeform + deformPadding);
        }
        else
        {
            scatterYAxis_->setRange(0, 1);
        }
    }

    void ExperimentMonitoringTab::updateHistogram(const std::vector<backend::services::ProcessedFrame> &validFrames)
    {
        // Reset series
#if MIB_HAS_QHISTOGRAMSERIES
        if (histogramSeries_)
            histogramSeries_->clear();
#else
        if (barSeries_)
            barSeries_->clear();
#endif

        // Use fixed range for consistent comparison across datasets
        const double minVal = HISTOGRAM_MIN;
        const double maxVal = HISTOGRAM_MAX;
        const double binWidth = HISTOGRAM_BIN_WIDTH;

        // Always set fixed x-axis range regardless of data
#if MIB_HAS_QHISTOGRAMSERIES
        histogramXAxis_->setRange(minVal, maxVal);
        histogramXAxis_->setTickCount(6);
#else
        // For fallback, we'll set the category axis later
#endif

        // Collect ring ratio values from valid frames
        std::vector<double> ringRatios;
        if (!validFrames.empty())
        {
            for (const auto &frame : validFrames)
            {
                if (frame.validation.isValid && frame.validation.ringRatio > 0.0)
                {
                    ringRatios.push_back(frame.validation.ringRatio);
                }
            }
        }

        // If no data, show empty histogram with fixed range
        if (ringRatios.empty())
        {
            histogramYAxis_->setRange(0, 1);
#if !MIB_HAS_QHISTOGRAMSERIES
            // For fallback, create empty category axis with fixed range
            if (histogramCategoryAxis_)
            {
                histogramChart_->removeAxis(histogramCategoryAxis_);
                delete histogramCategoryAxis_;
                histogramCategoryAxis_ = nullptr;
            }
            histogramCategoryAxis_ = new QBarCategoryAxis();
            QStringList categories;
            categories.reserve(HISTOGRAM_BINS);
            for (int i = 0; i < HISTOGRAM_BINS; ++i)
            {
                const double start = minVal + i * binWidth;
                const double end = (i == HISTOGRAM_BINS - 1) ? maxVal : (start + binWidth);
                categories << QString("%1-%2").arg(start, 0, 'f', 1).arg(end, 0, 'f', 1);
            }
            histogramCategoryAxis_->append(categories);
            histogramCategoryAxis_->setLabelsAngle(-90);
            histogramChart_->addAxis(histogramCategoryAxis_, Qt::AlignBottom);
            barSeries_->attachAxis(histogramCategoryAxis_);
            // Note: histogramYAxis_ is already attached in constructor, no need to reattach
#endif
            return;
        }

        // Count values in each bin (used for Y-axis headroom and fallback bar series)
        std::vector<int> binCounts(HISTOGRAM_BINS, 0);
        for (double val : ringRatios)
        {
            // Clamp values outside the fixed range to first or last bin
            double clampedVal = std::clamp(val, minVal, maxVal);
            int binIndex = static_cast<int>((clampedVal - minVal) / binWidth);
            // Ensure binIndex is within valid range (handle edge case where clampedVal == maxVal)
            if (binIndex >= HISTOGRAM_BINS)
            {
                binIndex = HISTOGRAM_BINS - 1;
            }
            binIndex = std::clamp(binIndex, 0, HISTOGRAM_BINS - 1);
            binCounts[binIndex]++;
        }

        int maxCount = 0;
        for (int count : binCounts)
        {
            maxCount = std::max(maxCount, count);
        }

        // Set Y-axis range (with headroom for empty data case)
        const int yMax = std::max(1, static_cast<int>(std::ceil(maxCount * 1.1)));
        histogramYAxis_->setRange(0, yMax);
        histogramYAxis_->applyNiceNumbers();

#if MIB_HAS_QHISTOGRAMSERIES
        // Populate histogram series with samples and set bins
        if (histogramSeries_)
        {
            QVector<qreal> samples;
            samples.reserve(static_cast<int>(ringRatios.size()));
            for (double v : ringRatios)
            {
                samples.append(static_cast<qreal>(v));
            }
            histogramSeries_->setBinsCount(HISTOGRAM_BINS);
            histogramSeries_->setSamples(samples);
        }
        // X-axis range already set above (fixed range for consistent comparison)
#else
        // Fallback: build bar set and category axis
        auto *barSet = new QBarSet("");
        for (int count : binCounts)
        {
            *barSet << count;
        }
        barSeries_->append(barSet);
        // Build/update category axis labels to match bins
        if (histogramCategoryAxis_)
        {
            histogramChart_->removeAxis(histogramCategoryAxis_);
            delete histogramCategoryAxis_;
            histogramCategoryAxis_ = nullptr;
        }
        histogramCategoryAxis_ = new QBarCategoryAxis();
        {
            QStringList categories;
            categories.reserve(HISTOGRAM_BINS);
            for (int i = 0; i < HISTOGRAM_BINS; ++i)
            {
                const double start = minVal + i * binWidth;
                const double end = (i == HISTOGRAM_BINS - 1) ? maxVal : (start + binWidth);
                categories << QString("%1-%2").arg(start, 0, 'f', 1).arg(end, 0, 'f', 1);
            }
            histogramCategoryAxis_->append(categories);
        }
        histogramCategoryAxis_->setLabelsAngle(-90);
        histogramChart_->addAxis(histogramCategoryAxis_, Qt::AlignBottom);
        barSeries_->attachAxis(histogramCategoryAxis_);
        // Note: histogramYAxis_ is already attached in constructor, no need to reattach
#endif
    }

    void ExperimentMonitoringTab::updateValidFramesGrid(const std::vector<backend::services::ProcessedFrame> &validFrames)
    {
        clearGrid(validFramesGrid_);

        // Get ROI
        auto roi = backend_.processing().getRealtimeRoi();
        if (roi.w <= 0 || roi.h <= 0)
        {
            return;
        }

        // Get last MAX_FRAMES_TO_SHOW valid frames
        size_t startIdx = validFrames.size() > MAX_FRAMES_TO_SHOW
                              ? validFrames.size() - MAX_FRAMES_TO_SHOW
                              : 0;

        int row = 0;
        int col = 0;
        for (size_t i = startIdx; i < validFrames.size(); ++i)
        {
            const auto &frame = validFrames[i];
            QImage roiImage;

            if (showValidOverlay_ && !frame.processedImage.empty())
            {
                // If monitoring stores ROI-only images, use them directly. Otherwise, crop.
                bool alreadyRoi = (frame.originalImage.cols == roi.w && frame.originalImage.rows == roi.h);
                cv::Mat roiOriginal = alreadyRoi ? frame.originalImage : frame.originalImage(cv::Rect(roi.x, roi.y, roi.w, roi.h));
                cv::Mat roiMask = alreadyRoi ? frame.processedImage : frame.processedImage(cv::Rect(roi.x, roi.y, roi.w, roi.h));
                roiImage = createOverlayImage(roiOriginal, roiMask);
            }
            else
            {
                // If already ROI, convert full; else extract
                if (frame.originalImage.cols == roi.w && frame.originalImage.rows == roi.h)
                {
                    roiImage = matToQImage(frame.originalImage);
                }
                else
                {
                    roiImage = extractRoiImage(frame.originalImage, roi.x, roi.y, roi.w, roi.h);
                }
            }

            if (!roiImage.isNull())
            {
                QPixmap pixmap = QPixmap::fromImage(roiImage.scaled(
                    THUMBNAIL_SIZE, THUMBNAIL_SIZE,
                    Qt::KeepAspectRatio, Qt::SmoothTransformation));

                QLabel *label = new QLabel(validFramesWidget_);
                label->setPixmap(pixmap);
                label->setAlignment(Qt::AlignCenter);
                label->setFrameStyle(QFrame::Box);
                label->setLineWidth(1);
                label->setStyleSheet("QLabel { border: 1px solid gray; }");
                label->setMinimumSize(THUMBNAIL_SIZE, THUMBNAIL_SIZE);
                label->setMaximumSize(THUMBNAIL_SIZE, THUMBNAIL_SIZE);
                label->setScaledContents(false);

                validFramesGrid_->addWidget(label, row, col);

                col++;
                if (col >= GRID_COLUMNS)
                {
                    col = 0;
                    row++;
                }
            }
        }
    }

    void ExperimentMonitoringTab::updateInvalidFramesGrid(const std::vector<backend::services::ProcessedFrame> &invalidFrames)
    {
        clearGrid(invalidFramesGrid_);

        // Get ROI
        auto roi = backend_.processing().getRealtimeRoi();
        if (roi.w <= 0 || roi.h <= 0)
        {
            return;
        }

        // Get last MAX_FRAMES_TO_SHOW invalid frames
        size_t startIdx = invalidFrames.size() > MAX_FRAMES_TO_SHOW
                              ? invalidFrames.size() - MAX_FRAMES_TO_SHOW
                              : 0;

        int row = 0;
        int col = 0;
        for (size_t i = startIdx; i < invalidFrames.size(); ++i)
        {
            const auto &frame = invalidFrames[i];
            QImage roiImage;

            if (showInvalidOverlay_ && !frame.processedImage.empty())
            {
                bool alreadyRoi = (frame.originalImage.cols == roi.w && frame.originalImage.rows == roi.h);
                cv::Mat roiOriginal = alreadyRoi ? frame.originalImage : frame.originalImage(cv::Rect(roi.x, roi.y, roi.w, roi.h));
                cv::Mat roiMask = alreadyRoi ? frame.processedImage : frame.processedImage(cv::Rect(roi.x, roi.y, roi.w, roi.h));
                roiImage = createOverlayImage(roiOriginal, roiMask);
            }
            else
            {
                if (frame.originalImage.cols == roi.w && frame.originalImage.rows == roi.h)
                {
                    roiImage = matToQImage(frame.originalImage);
                }
                else
                {
                    roiImage = extractRoiImage(frame.originalImage, roi.x, roi.y, roi.w, roi.h);
                }
            }

            if (!roiImage.isNull())
            {
                QPixmap pixmap = QPixmap::fromImage(roiImage.scaled(
                    THUMBNAIL_SIZE, THUMBNAIL_SIZE,
                    Qt::KeepAspectRatio, Qt::SmoothTransformation));

                QLabel *label = new QLabel(invalidFramesWidget_);
                label->setPixmap(pixmap);
                label->setAlignment(Qt::AlignCenter);
                label->setFrameStyle(QFrame::Box);
                label->setLineWidth(1);
                label->setStyleSheet("QLabel { border: 1px solid gray; }");
                label->setMinimumSize(THUMBNAIL_SIZE, THUMBNAIL_SIZE);
                label->setMaximumSize(THUMBNAIL_SIZE, THUMBNAIL_SIZE);
                label->setScaledContents(false);

                invalidFramesGrid_->addWidget(label, row, col);

                col++;
                if (col >= GRID_COLUMNS)
                {
                    col = 0;
                    row++;
                }
            }
        }
    }

    QImage ExperimentMonitoringTab::extractRoiImage(const cv::Mat &image, int x, int y, int w, int h) const
    {
        if (image.empty())
        {
            return QImage();
        }

        // Ensure ROI is within image bounds
        int imgWidth = image.cols;
        int imgHeight = image.rows;
        int roiX = std::max(0, std::min(x, imgWidth - 1));
        int roiY = std::max(0, std::min(y, imgHeight - 1));
        int roiW = std::min(w, imgWidth - roiX);
        int roiH = std::min(h, imgHeight - roiY);

        if (roiW <= 0 || roiH <= 0)
        {
            return QImage();
        }

        cv::Rect roiRect(roiX, roiY, roiW, roiH);
        cv::Mat roiMat = image(roiRect);

        return matToQImage(roiMat);
    }

    QImage ExperimentMonitoringTab::matToQImage(const cv::Mat &mat) const
    {
        if (mat.empty())
        {
            return QImage();
        }

        if (mat.type() == CV_8UC1)
        {
            // Grayscale
            QImage img(mat.data, mat.cols, mat.rows, static_cast<int>(mat.step), QImage::Format_Grayscale8);
            return img.copy();
        }
        else if (mat.type() == CV_8UC3)
        {
            // BGR to RGB
            cv::Mat rgb;
            cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
            QImage img(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888);
            return img.copy();
        }
        else if (mat.type() == CV_8UC4)
        {
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

    void ExperimentMonitoringTab::clearGrid(QGridLayout *grid)
    {
        // Remove all widgets from the grid
        QLayoutItem *item;
        while ((item = grid->takeAt(0)) != nullptr)
        {
            if (item->widget())
            {
                item->widget()->deleteLater();
            }
            delete item;
        }
    }

    void ExperimentMonitoringTab::onToggleOverlay(bool enabled)
    {
        QCheckBox *sender = qobject_cast<QCheckBox *>(this->sender());
        if (sender == validOverlayCheck_)
        {
            showValidOverlay_ = enabled;
            // Trigger update to refresh grid with overlay
            updateValidFramesGrid(recentValidFrames_);
        }
        else if (sender == invalidOverlayCheck_)
        {
            showInvalidOverlay_ = enabled;
            // Trigger update to refresh grid with overlay
            updateInvalidFramesGrid(recentInvalidFrames_);
        }
    }

    void ExperimentMonitoringTab::onClearBuffer()
    {
        int ret = QMessageBox::question(this, tr("Clear Buffer"),
                                        tr("Are you sure you want to clear the monitoring buffer? This will remove all accumulated frames."),
                                        QMessageBox::Yes | QMessageBox::No,
                                        QMessageBox::No);
        if (ret == QMessageBox::Yes)
        {
            backend_.processing().clearMonitoringFrames();
            recentValidFrames_.clear();
            recentInvalidFrames_.clear();
            lastValidFrameIndex_ = 0;
            lastInvalidFrameIndex_ = 0;
            // Update displays
            updateScatterplot(recentValidFrames_);
            updateHistogram(recentValidFrames_);
            updateValidFramesGrid(recentValidFrames_);
            updateInvalidFramesGrid(recentInvalidFrames_);
            SPDLOG_INFO("Monitoring buffer cleared");
        }
    }

    QImage ExperimentMonitoringTab::createOverlayImage(const cv::Mat &original, const cv::Mat &mask) const
    {
        if (original.empty() || mask.empty())
        {
            return QImage();
        }

        // Convert original to RGB if needed
        cv::Mat rgb;
        if (original.channels() == 1)
        {
            cv::cvtColor(original, rgb, cv::COLOR_GRAY2RGB);
        }
        else
        {
            rgb = original.clone();
            if (rgb.channels() == 3)
            {
                cv::cvtColor(rgb, rgb, cv::COLOR_BGR2RGB);
            }
        }

        // Create overlay: green tint where mask is non-zero
        cv::Mat overlay = rgb.clone();
        for (int y = 0; y < overlay.rows && y < mask.rows; ++y)
        {
            for (int x = 0; x < overlay.cols && x < mask.cols; ++x)
            {
                if (mask.at<uchar>(y, x) > 0)
                {
                    cv::Vec3b &pixel = overlay.at<cv::Vec3b>(y, x);
                    // Blend with green (0, 255, 0) at 30% opacity
                    pixel[0] = static_cast<uchar>(pixel[0] * 0.7);                                // R
                    pixel[1] = static_cast<uchar>(std::min(255.0, pixel[1] * 0.7 + 255.0 * 0.3)); // G
                    pixel[2] = static_cast<uchar>(pixel[2] * 0.7);                                // B
                }
            }
        }

        QImage img(overlay.data, overlay.cols, overlay.rows, static_cast<int>(overlay.step), QImage::Format_RGB888);
        return img.copy();
    }

    std::vector<std::vector<double>> ExperimentMonitoringTab::computeKDE(const std::vector<std::pair<double, double>> &points,
                                                                         int gridX, int gridY, double bandwidth) const
    {
        if (points.empty() || bandwidth <= 0.0)
        {
            return std::vector<std::vector<double>>(gridY, std::vector<double>(gridX, 0.0));
        }

        // Find data range
        double minX = std::numeric_limits<double>::max();
        double maxX = std::numeric_limits<double>::lowest();
        double minY = std::numeric_limits<double>::max();
        double maxY = std::numeric_limits<double>::lowest();

        for (const auto &p : points)
        {
            minX = std::min(minX, p.first);
            maxX = std::max(maxX, p.first);
            minY = std::min(minY, p.second);
            maxY = std::max(maxY, p.second);
        }

        if (minX >= maxX || minY >= maxY)
        {
            return std::vector<std::vector<double>>(gridY, std::vector<double>(gridX, 0.0));
        }

        // Add padding
        double rangeX = maxX - minX;
        double rangeY = maxY - minY;
        minX -= rangeX * 0.1;
        maxX += rangeX * 0.1;
        minY -= rangeY * 0.1;
        maxY += rangeY * 0.1;

        // Initialize density grid
        std::vector<std::vector<double>> density(gridY, std::vector<double>(gridX, 0.0));

        double stepX = (maxX - minX) / gridX;
        double stepY = (maxY - minY) / gridY;

        // Compute KDE using Gaussian kernel
        const double sqrt2pi = std::sqrt(2.0 * M_PI);
        const double bandwidth2 = bandwidth * bandwidth;

        for (int gy = 0; gy < gridY; ++gy)
        {
            double gridYVal = minY + (gy + 0.5) * stepY;
            for (int gx = 0; gx < gridX; ++gx)
            {
                double gridXVal = minX + (gx + 0.5) * stepX;

                double sum = 0.0;
                for (const auto &p : points)
                {
                    double dx = p.first - gridXVal;
                    double dy = p.second - gridYVal;
                    double dist2 = dx * dx + dy * dy;
                    // Gaussian kernel
                    sum += std::exp(-dist2 / (2.0 * bandwidth2)) / (sqrt2pi * bandwidth);
                }
                density[gy][gx] = sum / points.size();
            }
        }

        return density;
    }

    void ExperimentMonitoringTab::setKdeBandwidth(double bandwidth)
    {
        kdeBandwidth_ = bandwidth;
        // Trigger update to refresh scatterplot
        updateScatterplot(recentValidFrames_);
    }

    void ExperimentMonitoringTab::setKdeGridResolution(int resolution)
    {
        kdeGridResolution_ = resolution;
        // Trigger update to refresh scatterplot
        updateScatterplot(recentValidFrames_);
    }

} // namespace frontend

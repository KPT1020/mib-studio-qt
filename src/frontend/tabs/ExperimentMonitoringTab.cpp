#include "frontend/tabs/ExperimentMonitoringTab.h"
#include "ui_ExperimentMonitoringTab.h"

#include <QTimer>
#include <QLabel>
#include <QPixmap>
#include <QPainter>
#include <QChartView>
#include <QScatterSeries>
#include <QLineSeries>
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
#include <QMessageBox>
#include <QShowEvent>
#include <QHideEvent>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <numeric>
#include <map>
#include <QFile>
#include <QTextStream>
#include <QCoreApplication>
#include <QDir>
#include <QPixmap>
#include <QFileDialog>

#include "backend/AppBackend.h"
#include "backend/services/ProcessingService.h"

#include <spdlog/spdlog.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

namespace frontend
{

    ExperimentMonitoringTab::ExperimentMonitoringTab(backend::AppBackend &backend, QWidget *parent)
        : QWidget(parent), ui(new Ui::ExperimentMonitoringTab), backend_(backend)
    {
        ui->setupUi(this);

        roiLabel_ = new QLabel(tr("ROI: --"), this);
        roiLabel_->setStyleSheet("font-weight: bold; padding: 0 8px;");
        ui->topRowLayout->addWidget(roiLabel_);

        setupCharts();

        // Connect signals
        connect(ui->clearBufferBtn, &QPushButton::clicked, this, &ExperimentMonitoringTab::onClearBuffer);
        connect(ui->validOverlayCheck, &QCheckBox::toggled, this, &ExperimentMonitoringTab::onToggleOverlay);
        connect(ui->invalidOverlayCheck, &QCheckBox::toggled, this, &ExperimentMonitoringTab::onToggleOverlay);
        connect(ui->autoTuneBtn, &QPushButton::clicked, this, &ExperimentMonitoringTab::onAutoTune);

        // Set column stretch to make panels equal size
        ui->gridLayout->setColumnStretch(0, 1);
        ui->gridLayout->setColumnStretch(1, 1);
        ui->gridLayout->setRowStretch(1, 1); // Charts row
        ui->gridLayout->setRowStretch(2, 1); // Frame grids row

        // Setup update timer
        updateTimer_ = new QTimer(this);
        updateTimer_->setInterval(UPDATE_INTERVAL_MS);
        connect(updateTimer_, &QTimer::timeout, this, &ExperimentMonitoringTab::onUpdate);
    }

    void ExperimentMonitoringTab::updateRoiDisplay(int offsetX, int offsetY, int width, int height) {
        if (roiLabel_)
            roiLabel_->setText(tr("ROI: %1 x %2 @ (%3, %4)").arg(width).arg(height).arg(offsetX).arg(offsetY));
    }

    ExperimentMonitoringTab::~ExperimentMonitoringTab() {
        // Cleanup isoelastic curve line series
        for (QLineSeries* series : isoelasticCurves_)
        {
            if (series)
            {
                delete series;
            }
        }
        isoelasticCurves_.clear();
        delete ui;
    }

    void ExperimentMonitoringTab::setupCharts() {
        // Panel 1: Scatterplot (top-left)
        scatterplotChart_ = new QChart();
        scatterSeries_ = new QScatterSeries();
        scatterSeries_->setMarkerSize(6.0);
        scatterSeries_->setName("Valid Frames");
        scatterplotChart_->addSeries(scatterSeries_);
        scatterplotChart_->setTitle("Deformability vs Area (μm²)");
        scatterplotChart_->legend()->setVisible(false);

        scatterXAxis_ = new QValueAxis();
        scatterXAxis_->setTitleText("Area (μm²)");
        scatterYAxis_ = new QValueAxis();
        scatterYAxis_->setTitleText("Deformability");
        scatterplotChart_->addAxis(scatterXAxis_, Qt::AlignBottom);
        scatterplotChart_->addAxis(scatterYAxis_, Qt::AlignLeft);
        scatterSeries_->attachAxis(scatterXAxis_);
        scatterSeries_->attachAxis(scatterYAxis_);
        scatterXAxis_->setRange(scatterXMin_, scatterXMax_);
        scatterYAxis_->setRange(scatterYMin_, scatterYMax_);

        scatterplotView_ = new QChartView(scatterplotChart_);
        scatterplotView_->setRenderHint(QPainter::Antialiasing);
        // Replace placeholder with actual chart view
        ui->gridLayout->removeWidget(ui->scatterplotViewPlaceholder);
        delete ui->scatterplotViewPlaceholder;
        ui->gridLayout->addWidget(scatterplotView_, 1, 0);

        // Load isoelastic curves overlay
        loadIsoelasticCurves();

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

        if (histogramXAxis_)
            histogramXAxis_->setRange(histogramXMin_, histogramXMax_);
        histogramYAxis_->setRange(0, std::max(1.0, histogramYMax_));

        histogramView_ = new QChartView(histogramChart_);
        histogramView_->setRenderHint(QPainter::Antialiasing);
        // Replace placeholder with actual chart view
        ui->gridLayout->removeWidget(ui->histogramViewPlaceholder);
        delete ui->histogramViewPlaceholder;
        ui->gridLayout->addWidget(histogramView_, 1, 1);
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

        const double conversionFactor = backend_.processing().getPixelToMicronFactor();
        const double areaConversionFactor = conversionFactor * conversionFactor;

        for (const auto &frame : validFrames)
        {
            if (frame.validation.isValid)
            {
                double areaMicrons = frame.validation.area * areaConversionFactor;
                double deform = frame.validation.deformability;
                scatterSeries_->append(areaMicrons, deform);
            }
        }

        scatterXAxis_->setRange(scatterXMin_, scatterXMax_);
        scatterYAxis_->setRange(scatterYMin_, scatterYMax_);
    }

    void ExperimentMonitoringTab::updateHistogram(const std::vector<backend::services::ProcessedFrame> &validFrames)
    {
#if MIB_HAS_QHISTOGRAMSERIES
        if (histogramSeries_)
            histogramSeries_->clear();
#else
        if (barSeries_)
            barSeries_->clear();
#endif

        const double minVal = histogramXMin_;
        const double maxVal = histogramXMax_;
        const double binWidth = histogramBinWidth_;
        const int histogramBins = std::max(1, static_cast<int>(std::round((maxVal - minVal) / binWidth)));

        if (histogramXAxis_)
        {
            histogramXAxis_->setRange(minVal, maxVal);
            histogramXAxis_->setTickCount(6);
        }

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

        const double yMax = std::max(1.0, histogramYMax_);
        histogramYAxis_->setRange(0, yMax);

        if (ringRatios.empty())
        {
#if !MIB_HAS_QHISTOGRAMSERIES
            if (histogramCategoryAxis_)
            {
                histogramChart_->removeAxis(histogramCategoryAxis_);
                delete histogramCategoryAxis_;
                histogramCategoryAxis_ = nullptr;
            }
            histogramCategoryAxis_ = new QBarCategoryAxis();
            QStringList categories;
            categories.reserve(histogramBins);
            for (int i = 0; i < histogramBins; ++i)
            {
                const double start = minVal + i * binWidth;
                const double end = (i == histogramBins - 1) ? maxVal : (start + binWidth);
                categories << QString("%1-%2").arg(start, 0, 'f', 1).arg(end, 0, 'f', 1);
            }
            histogramCategoryAxis_->append(categories);
            histogramCategoryAxis_->setLabelsAngle(-90);
            histogramChart_->addAxis(histogramCategoryAxis_, Qt::AlignBottom);
            barSeries_->attachAxis(histogramCategoryAxis_);
#endif
            return;
        }

        std::vector<int> binCounts(static_cast<size_t>(histogramBins), 0);
        for (double val : ringRatios)
        {
            double clampedVal = std::clamp(val, minVal, maxVal);
            int binIndex = static_cast<int>((clampedVal - minVal) / binWidth);
            if (binIndex >= histogramBins)
                binIndex = histogramBins - 1;
            binIndex = std::clamp(binIndex, 0, histogramBins - 1);
            binCounts[static_cast<size_t>(binIndex)]++;
        }

#if MIB_HAS_QHISTOGRAMSERIES
        if (histogramSeries_)
        {
            QVector<qreal> samples;
            samples.reserve(static_cast<int>(ringRatios.size()));
            for (double v : ringRatios)
            {
                samples.append(static_cast<qreal>(v));
            }
            histogramSeries_->setBinsCount(histogramBins);
            histogramSeries_->setSamples(samples);
        }
#else
        auto *barSet = new QBarSet("");
        for (int count : binCounts)
        {
            *barSet << count;
        }
        barSeries_->append(barSet);
        if (histogramCategoryAxis_)
        {
            histogramChart_->removeAxis(histogramCategoryAxis_);
            delete histogramCategoryAxis_;
            histogramCategoryAxis_ = nullptr;
        }
        histogramCategoryAxis_ = new QBarCategoryAxis();
        {
            QStringList categories;
            categories.reserve(histogramBins);
            for (int i = 0; i < histogramBins; ++i)
            {
                const double start = minVal + i * binWidth;
                const double end = (i == histogramBins - 1) ? maxVal : (start + binWidth);
                categories << QString("%1-%2").arg(start, 0, 'f', 1).arg(end, 0, 'f', 1);
            }
            histogramCategoryAxis_->append(categories);
        }
        histogramCategoryAxis_->setLabelsAngle(-90);
        histogramChart_->addAxis(histogramCategoryAxis_, Qt::AlignBottom);
        barSeries_->attachAxis(histogramCategoryAxis_);
#endif
    }

    void ExperimentMonitoringTab::updateValidFramesGrid(const std::vector<backend::services::ProcessedFrame> &validFrames)
    {
        clearGrid(ui->validFramesGrid);

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

                QLabel *label = new QLabel(ui->validFramesWidget);
                label->setPixmap(pixmap);
                label->setAlignment(Qt::AlignCenter);
                label->setFrameStyle(QFrame::Box);
                label->setLineWidth(1);
                label->setStyleSheet("QLabel { border: 1px solid gray; }");
                label->setMinimumSize(THUMBNAIL_SIZE, THUMBNAIL_SIZE);
                label->setMaximumSize(THUMBNAIL_SIZE, THUMBNAIL_SIZE);
                label->setScaledContents(false);

                ui->validFramesGrid->addWidget(label, row, col);

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
        clearGrid(ui->invalidFramesGrid);

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

                QLabel *label = new QLabel(ui->invalidFramesWidget);
                label->setPixmap(pixmap);
                label->setAlignment(Qt::AlignCenter);
                label->setFrameStyle(QFrame::Box);
                label->setLineWidth(1);
                label->setStyleSheet("QLabel { border: 1px solid gray; }");
                label->setMinimumSize(THUMBNAIL_SIZE, THUMBNAIL_SIZE);
                label->setMaximumSize(THUMBNAIL_SIZE, THUMBNAIL_SIZE);
                label->setScaledContents(false);

                ui->invalidFramesGrid->addWidget(label, row, col);

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
        if (sender == ui->validOverlayCheck)
        {
            showValidOverlay_ = enabled;
            // Trigger update to refresh grid with overlay
            updateValidFramesGrid(recentValidFrames_);
        }
        else if (sender == ui->invalidOverlayCheck)
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

    void ExperimentMonitoringTab::onAutoTune()
    {
        auto result = backend_.processing().computeAutoTune();
        if (!result.success) {
            QMessageBox::warning(this, tr("Auto Tune"), QString::fromStdString(result.message));
            return;
        }

        const auto& cfg = result.suggestedConfig;
        double pxToUm2 = backend_.processing().getPixelToMicronFactor();
        double areaFactor = pxToUm2 * pxToUm2; // pixel area to micron^2

        QString details = tr(
            "Auto Tune analyzed %1 frames.\n\n"
            "Suggested gating thresholds:\n\n"
            "  Area: %2 - %3 px  (%4 - %5 um^2)\n"
            "  Deformability: %6 - %7\n"
            "  Ring Ratio: %8 - %9\n"
            "  Area Ratio max: %10\n\n"
            "Statistics (median [Q1 - Q3]):\n"
            "  Area: %11 [%12 - %13]\n"
            "  Deformability: %14 [%15 - %16]\n"
            "  Ring Ratio: %17 [%18 - %19]\n\n"
            "Apply these thresholds?")
            .arg(static_cast<qulonglong>(result.framesAnalyzed))
            .arg(cfg.area_threshold_min).arg(cfg.area_threshold_max)
            .arg(cfg.area_threshold_min * areaFactor, 0, 'f', 1)
            .arg(cfg.area_threshold_max * areaFactor, 0, 'f', 1)
            .arg(cfg.deformability_threshold_min, 0, 'f', 3)
            .arg(cfg.deformability_threshold_max, 0, 'f', 3)
            .arg(cfg.ring_ratio_min, 0, 'f', 1)
            .arg(cfg.ring_ratio_max, 0, 'f', 1)
            .arg(cfg.area_ratio_threshold_max, 0, 'f', 2)
            .arg(result.area.median, 0, 'f', 1)
            .arg(result.area.q1, 0, 'f', 1).arg(result.area.q3, 0, 'f', 1)
            .arg(result.deformability.median, 0, 'f', 3)
            .arg(result.deformability.q1, 0, 'f', 3).arg(result.deformability.q3, 0, 'f', 3)
            .arg(result.ringRatio.median, 0, 'f', 1)
            .arg(result.ringRatio.q1, 0, 'f', 1).arg(result.ringRatio.q3, 0, 'f', 1);

        int ret = QMessageBox::question(this, tr("Auto Tune - Confirm"),
                                        details,
                                        QMessageBox::Yes | QMessageBox::No,
                                        QMessageBox::No);
        if (ret == QMessageBox::Yes) {
            backend_.processing().setProcessingConfig(cfg);
            SPDLOG_INFO("Auto Tune applied: area=[{}, {}], deform=[{:.3f}, {:.3f}], "
                        "ringRatio=[{:.1f}, {:.1f}], areaRatio_max={:.2f}",
                        cfg.area_threshold_min, cfg.area_threshold_max,
                        cfg.deformability_threshold_min, cfg.deformability_threshold_max,
                        cfg.ring_ratio_min, cfg.ring_ratio_max,
                        cfg.area_ratio_threshold_max);
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

    void ExperimentMonitoringTab::setScatterXRange(double minVal, double maxVal)
    {
        if (minVal >= maxVal)
            return;
        scatterXMin_ = minVal;
        scatterXMax_ = maxVal;
    }

    void ExperimentMonitoringTab::setScatterYRange(double minVal, double maxVal)
    {
        if (minVal >= maxVal)
            return;
        scatterYMin_ = minVal;
        scatterYMax_ = maxVal;
    }

    void ExperimentMonitoringTab::setHistogramXRange(double minVal, double maxVal)
    {
        if (minVal >= maxVal)
            return;
        histogramXMin_ = minVal;
        histogramXMax_ = maxVal;
    }

    void ExperimentMonitoringTab::setHistogramYMax(double maxVal)
    {
        if (maxVal <= 0)
            return;
        histogramYMax_ = maxVal;
    }

    void ExperimentMonitoringTab::setHistogramBinWidth(double width)
    {
        if (width <= 0)
            return;
        histogramBinWidth_ = width;
    }

    void ExperimentMonitoringTab::refreshCharts()
    {
        updateScatterplot(recentValidFrames_);
        updateHistogram(recentValidFrames_);
    }

    void ExperimentMonitoringTab::loadIsoelasticCurves()
    {
        // Find the isoelastic curve data file
        QString appDir = QCoreApplication::applicationDirPath();
        QString filePath = QDir(appDir).absoluteFilePath("../resources/isoelastic_curve/scaled_isoelastic_data_6.16-4.24.txt");
        
        // Try alternative path if file doesn't exist
        if (!QFile::exists(filePath))
        {
            filePath = QDir(appDir).absoluteFilePath("resources/isoelastic_curve/scaled_isoelastic_data_6.16-4.24.txt");
        }
        
        // Try source directory path for development
        if (!QFile::exists(filePath))
        {
            filePath = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../../resources/isoelastic_curve/scaled_isoelastic_data_6.16-4.24.txt");
        }

        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            SPDLOG_WARN("Failed to open isoelastic curve file: {}", filePath.toStdString());
            return;
        }

        // Group data points by emodulus value
        std::map<double, std::vector<std::pair<double, double>>> curvesByModulus;

        QTextStream in(&file);
        while (!in.atEnd())
        {
            QString line = in.readLine().trimmed();
            
            // Skip empty lines and comments
            if (line.isEmpty() || line.startsWith('#'))
            {
                continue;
            }

            // Parse tab-separated values: area_um, deform, emodulus
            QStringList parts = line.split('\t', Qt::SkipEmptyParts);
            if (parts.size() < 3)
            {
                continue;
            }

            bool ok1, ok2, ok3;
            double areaUm = parts[0].toDouble(&ok1);
            double deform = parts[1].toDouble(&ok2);
            double emodulus = parts[2].toDouble(&ok3);

            if (ok1 && ok2 && ok3)
            {
                curvesByModulus[emodulus].push_back({areaUm, deform});
            }
        }

        file.close();

        if (curvesByModulus.empty())
        {
            SPDLOG_WARN("No isoelastic curve data found in file: {}", filePath.toStdString());
            return;
        }

        // Create QLineSeries for each modulus value (in reverse order for legend)
        for (auto it = curvesByModulus.rbegin(); it != curvesByModulus.rend(); ++it)
        {
            const auto& [emodulus, points] = *it;
            QLineSeries* series = new QLineSeries();
            series->setName(QString("%1 kPa").arg(emodulus, 0, 'f', 2));
            
            // Add points to series
            for (const auto& [area, deform] : points)
            {
                series->append(area, deform);
            }

            // Add series to chart
            scatterplotChart_->addSeries(series);
            series->attachAxis(scatterXAxis_);
            series->attachAxis(scatterYAxis_);
            
            // Store pointer for cleanup
            isoelasticCurves_.push_back(series);
        }

        // Enable legend to show all series and position it on the right
        scatterplotChart_->legend()->setVisible(true);
        scatterplotChart_->legend()->setAlignment(Qt::AlignRight);
        
        SPDLOG_INFO("Loaded {} isoelastic curves from {}", curvesByModulus.size(), filePath.toStdString());
    }

    bool ExperimentMonitoringTab::captureChartSnapshots(cv::Mat& histogramImage, cv::Mat& scatterPlotImage) const
    {
        histogramImage.release();
        scatterPlotImage.release();

        if (!histogramView_ || !scatterplotView_)
        {
            SPDLOG_WARN("Chart views not available for snapshot capture");
            return false;
        }

        // Capture histogram chart
        QPixmap histogramPixmap = histogramView_->grab();
        if (histogramPixmap.isNull())
        {
            SPDLOG_WARN("Failed to grab histogram chart");
            return false;
        }

        // Capture scatter plot chart
        QPixmap scatterPlotPixmap = scatterplotView_->grab();
        if (scatterPlotPixmap.isNull())
        {
            SPDLOG_WARN("Failed to grab scatter plot chart");
            return false;
        }

        // Convert QPixmap to QImage
        QImage histogramQImage = histogramPixmap.toImage();
        QImage scatterPlotQImage = scatterPlotPixmap.toImage();

        // Convert QImage to cv::Mat
        // QImage uses ARGB32 format, need to convert to BGR for OpenCV
        histogramQImage = histogramQImage.convertToFormat(QImage::Format_RGB32);
        scatterPlotQImage = scatterPlotQImage.convertToFormat(QImage::Format_RGB32);

        // Create cv::Mat from QImage
        histogramImage = cv::Mat(histogramQImage.height(), histogramQImage.width(), CV_8UC4, 
                                  const_cast<uchar*>(histogramQImage.constBits()), 
                                  histogramQImage.bytesPerLine()).clone();
        scatterPlotImage = cv::Mat(scatterPlotQImage.height(), scatterPlotQImage.width(), CV_8UC4,
                                   const_cast<uchar*>(scatterPlotQImage.constBits()),
                                   scatterPlotQImage.bytesPerLine()).clone();

        // Convert from RGBA to BGR
        cv::cvtColor(histogramImage, histogramImage, cv::COLOR_RGBA2BGR);
        cv::cvtColor(scatterPlotImage, scatterPlotImage, cv::COLOR_RGBA2BGR);

        SPDLOG_DEBUG("Captured chart snapshots: histogram {}x{}, scatter plot {}x{}",
                     histogramImage.cols, histogramImage.rows,
                     scatterPlotImage.cols, scatterPlotImage.rows);
        return true;
    }

    bool ExperimentMonitoringTab::exportChartAsTiff(QChartView* chartView, const QString& filePath) const
    {
        if (!chartView)
        {
            SPDLOG_ERROR("Chart view is null");
            return false;
        }

        // Grab chart as pixmap
        QPixmap pixmap = chartView->grab();
        if (pixmap.isNull())
        {
            SPDLOG_ERROR("Failed to grab chart view");
            return false;
        }

        // Convert QPixmap to QImage
        QImage qImage = pixmap.toImage();
        qImage = qImage.convertToFormat(QImage::Format_RGB32);

        // Convert QImage to cv::Mat
        cv::Mat mat(qImage.height(), qImage.width(), CV_8UC4, 
                    const_cast<uchar*>(qImage.constBits()), 
                    qImage.bytesPerLine());
        
        // Convert from RGBA to BGR
        cv::Mat bgrMat;
        cv::cvtColor(mat, bgrMat, cv::COLOR_RGBA2BGR);

        // Save as TIFF with compression
        std::vector<int> compression_params;
        compression_params.push_back(cv::IMWRITE_TIFF_COMPRESSION);
        compression_params.push_back(1); // LZW compression

        if (!cv::imwrite(filePath.toStdString(), bgrMat, compression_params))
        {
            SPDLOG_ERROR("Failed to write TIFF file: {}", filePath.toStdString());
            return false;
        }

        SPDLOG_INFO("Exported chart to TIFF: {}", filePath.toStdString());
        return true;
    }

    bool ExperimentMonitoringTab::exportHistogramAsTiff(const QString& filePath) const
    {
        return exportChartAsTiff(histogramView_, filePath);
    }

    bool ExperimentMonitoringTab::exportScatterPlotAsTiff(const QString& filePath) const
    {
        return exportChartAsTiff(scatterplotView_, filePath);
    }

} // namespace frontend

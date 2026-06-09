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
#include <QGroupBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QScrollArea>
#include <QVBoxLayout>
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

#include "frontend/widgets/ZoomableChartView.h"
#include "backend/app/AppBackend.h"
#include "backend/processing/ProcessingService.h"
#include "backend/services/TriggerService.h"

#include <spdlog/spdlog.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

namespace {

struct InvalidReason {
    QString shortText;
    QString longText;
};

std::vector<InvalidReason> getInvalidReasons(
    const backend::services::FilterResult& result,
    const backend::services::ProcessingConfig& config,
    double pixelToMicronFactor = 1.0)
{
    std::vector<InvalidReason> reasons;

    // Check early-exit conditions first (metrics not computed in these cases)
    if (config.require_single_inner_contour && result.innerContourCount == 0) {
        reasons.push_back({"No contour", "No inner contour found"});
        return reasons;
    }

    if (config.enable_border_check && result.touchesBorder) {
        reasons.push_back({"Border", "Contour touches ROI border"});
        // When border check fails, metrics are not computed — don't check them
        return reasons;
    }

    // Convert pixel area to μm² to match config threshold units
    double areaUm = result.area * pixelToMicronFactor * pixelToMicronFactor;

    // Metrics were computed — check which range checks failed
    if (config.enable_area_range_check) {
        if (areaUm < config.area_threshold_min || areaUm > config.area_threshold_max) {
            reasons.push_back({
                "Area",
                QString("Area: %1 μm² (range: %2-%3)")
                    .arg(areaUm, 0, 'f', 0)
                    .arg(config.area_threshold_min)
                    .arg(config.area_threshold_max)
            });
        }
    }

    if (config.enable_ring_ratio_check) {
        if (result.ringRatio <= config.ring_ratio_min || result.ringRatio >= config.ring_ratio_max) {
            reasons.push_back({
                "Ring",
                QString("Ring ratio: %1 (range: %2-%3)")
                    .arg(result.ringRatio, 0, 'f', 1)
                    .arg(config.ring_ratio_min, 0, 'f', 1)
                    .arg(config.ring_ratio_max, 0, 'f', 1)
            });
        }
    }

    if (config.enable_deformability_range_check) {
        if (result.deformability < config.deformability_threshold_min ||
            result.deformability > config.deformability_threshold_max) {
            reasons.push_back({
                "Deform",
                QString("Deformability: %1 (range: %2-%3)")
                    .arg(result.deformability, 0, 'f', 3)
                    .arg(config.deformability_threshold_min, 0, 'f', 3)
                    .arg(config.deformability_threshold_max, 0, 'f', 3)
            });
        }
    }

    if (config.enable_area_ratio_check) {
        if (result.areaRatio > config.area_ratio_threshold_max) {
            reasons.push_back({
                "Ratio",
                QString("Area ratio: %1 (max: %2)")
                    .arg(result.areaRatio, 0, 'f', 2)
                    .arg(config.area_ratio_threshold_max, 0, 'f', 2)
            });
        }
    }

    return reasons;
}

} // anonymous namespace

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
        setupTuneParamsPanel();

        // Connect signals
        connect(ui->clearBufferBtn, &QPushButton::clicked, this, &ExperimentMonitoringTab::onClearBuffer);
        connect(ui->sortTriggerBtn, &QPushButton::clicked, this, &ExperimentMonitoringTab::onSortTrigger);
        connect(ui->triggerDurationSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int us) {
            backend_.trigger().setPulseDurationUs(us);
        });

        // Setup periodic test trigger timer
        periodicTriggerTimer_ = new QTimer(this);
        periodicTriggerTimer_->setInterval(ui->periodicTriggerIntervalSpin->value());
        connect(periodicTriggerTimer_, &QTimer::timeout, this, [this]() {
            backend_.trigger().onTargetGroupResult(true);
            ++periodicTriggerPulseCount_;
        });
        connect(ui->periodicTriggerBtn, &QPushButton::toggled, this, &ExperimentMonitoringTab::onPeriodicTriggerToggled);
        connect(ui->periodicTriggerIntervalSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int ms) {
            if (periodicTriggerTimer_) periodicTriggerTimer_->setInterval(ms);
        });
        connect(ui->validOverlayCheck, &QCheckBox::toggled, this, &ExperimentMonitoringTab::onToggleOverlay);
        connect(ui->invalidOverlayCheck, &QCheckBox::toggled, this, &ExperimentMonitoringTab::onToggleOverlay);

        // Set column stretch to make panels equal size
        ui->gridLayout->setColumnStretch(0, 1);
        ui->gridLayout->setColumnStretch(1, 1);
        ui->gridLayout->setColumnStretch(2, 0); // Tune panel: minimum width
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

    void ExperimentMonitoringTab::setupTuneParamsPanel()
    {
        auto* placeholder = ui->tuneParamsPlaceholder;
        placeholder->setMinimumWidth(220);
        placeholder->setMaximumWidth(280);

        auto* outerLayout = new QVBoxLayout(placeholder);
        outerLayout->setContentsMargins(0, 0, 0, 0);
        outerLayout->setSpacing(0);

        // Scrollable content area (always visible)
        auto* scrollArea = new QScrollArea(placeholder);
        scrollArea->setWidgetResizable(true);
        scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scrollArea->setFrameShape(QFrame::NoFrame);

        tunePanelContent_ = new QWidget();
        auto* contentLayout = new QVBoxLayout(tunePanelContent_);
        contentLayout->setContentsMargins(4, 4, 4, 4);
        contentLayout->setSpacing(6);

        // --- Title ---
        auto* titleLabel = new QLabel(tr("<b>Tune Params</b>"), tunePanelContent_);
        contentLayout->addWidget(titleLabel);

        // --- Filter Thresholds ---
        auto* threshGroup = new QGroupBox(tr("Filter Thresholds"), tunePanelContent_);
        auto* threshLayout = new QVBoxLayout(threshGroup);
        threshLayout->setSpacing(4);

        auto addSpinRow = [](QVBoxLayout* layout, const QString& label, QSpinBox*& spin,
                             int min, int max, int step, int val) {
            auto* row = new QHBoxLayout();
            row->addWidget(new QLabel(label));
            spin = new QSpinBox();
            spin->setRange(min, max);
            spin->setSingleStep(step);
            spin->setValue(val);
            row->addWidget(spin);
            layout->addLayout(row);
        };

        auto addDblSpinRow = [](QVBoxLayout* layout, const QString& label, QDoubleSpinBox*& spin,
                                double min, double max, double step, int decimals, double val) {
            auto* row = new QHBoxLayout();
            row->addWidget(new QLabel(label));
            spin = new QDoubleSpinBox();
            spin->setRange(min, max);
            spin->setSingleStep(step);
            spin->setDecimals(decimals);
            spin->setValue(val);
            row->addWidget(spin);
            layout->addLayout(row);
        };

        addSpinRow(threshLayout, tr("Area Min"), areaMinSpin_, 0, 100000, 10, 250);
        addSpinRow(threshLayout, tr("Area Max"), areaMaxSpin_, 0, 100000, 10, 1000);
        addDblSpinRow(threshLayout, tr("Deform Min"), deformMinSpin_, 0.0, 1.0, 0.01, 2, 0.0);
        addDblSpinRow(threshLayout, tr("Deform Max"), deformMaxSpin_, 0.0, 1.0, 0.01, 2, 1.0);
        addDblSpinRow(threshLayout, tr("Area Ratio Max"), areaRatioMaxSpin_, 0.0, 10.0, 0.1, 2, 1.5);
        addDblSpinRow(threshLayout, tr("Ring Min"), ringMinSpin_, 0.0, 100.0, 0.5, 1, 15.0);
        addDblSpinRow(threshLayout, tr("Ring Max"), ringMaxSpin_, 0.0, 100.0, 0.5, 1, 25.0);
        contentLayout->addWidget(threshGroup);

        // --- Filter Enables ---
        auto* enableGroup = new QGroupBox(tr("Filter Enables"), tunePanelContent_);
        auto* enableLayout = new QVBoxLayout(enableGroup);
        enableLayout->setSpacing(2);

        borderCheckBox_ = new QCheckBox(tr("Border Check"));
        areaRangeCheckBox_ = new QCheckBox(tr("Area Range"));
        deformRangeCheckBox_ = new QCheckBox(tr("Deformability Range"));
        areaRatioCheckBox_ = new QCheckBox(tr("Area Ratio"));
        ringRatioCheckBox_ = new QCheckBox(tr("Ring Ratio"));
        singleInnerCheckBox_ = new QCheckBox(tr("Single Inner Contour"));

        enableLayout->addWidget(borderCheckBox_);
        enableLayout->addWidget(areaRangeCheckBox_);
        enableLayout->addWidget(deformRangeCheckBox_);
        enableLayout->addWidget(areaRatioCheckBox_);
        enableLayout->addWidget(ringRatioCheckBox_);
        enableLayout->addWidget(singleInnerCheckBox_);
        contentLayout->addWidget(enableGroup);

        // --- Target Group ---
        auto* targetGroup = new QGroupBox(tr("Target Group"), tunePanelContent_);
        auto* targetLayout = new QVBoxLayout(targetGroup);
        targetLayout->setSpacing(4);

        targetGroupEnableBox_ = new QCheckBox(tr("Enable"));
        targetLayout->addWidget(targetGroupEnableBox_);

        addSpinRow(targetLayout, tr("Area Min"), targetAreaMinSpin_, 0, 100000, 10, 300);
        addSpinRow(targetLayout, tr("Area Max"), targetAreaMaxSpin_, 0, 100000, 10, 800);
        addDblSpinRow(targetLayout, tr("Deform Min"), targetDeformMinSpin_, 0.0, 1.0, 0.01, 2, 0.0);
        addDblSpinRow(targetLayout, tr("Deform Max"), targetDeformMaxSpin_, 0.0, 1.0, 0.01, 2, 0.3);
        contentLayout->addWidget(targetGroup);

        // --- Multi-Image ---
        auto* multiImageGroup = new QGroupBox(tr("Multi-Image"), tunePanelContent_);
        auto* multiImageLayout = new QVBoxLayout(multiImageGroup);
        multiImageLayout->setSpacing(4);

        multiImageEnableBox_ = new QCheckBox(tr("Enable"));
        multiImageLayout->addWidget(multiImageEnableBox_);

        addSpinRow(multiImageLayout, tr("Images per trigger"), multiImageCountSpin_, 1, 32, 1, 1);
        contentLayout->addWidget(multiImageGroup);

        // --- Apply Button ---
        auto* applyBtn = new QPushButton(tr("Apply"), tunePanelContent_);
        connect(applyBtn, &QPushButton::clicked, this, &ExperimentMonitoringTab::onApplyParams);
        contentLayout->addWidget(applyBtn);

        contentLayout->addStretch(1);

        scrollArea->setWidget(tunePanelContent_);
        outerLayout->addWidget(scrollArea, 1);

        // Load current config values into widgets
        loadCurrentConfig();
    }

    void ExperimentMonitoringTab::loadCurrentConfig()
    {
        auto cfg = backend_.processing().getProcessingConfig();

        // Block signals to avoid triggering anything during load
        areaMinSpin_->setValue(cfg.area_threshold_min);
        areaMaxSpin_->setValue(cfg.area_threshold_max);
        deformMinSpin_->setValue(cfg.deformability_threshold_min);
        deformMaxSpin_->setValue(cfg.deformability_threshold_max);
        areaRatioMaxSpin_->setValue(cfg.area_ratio_threshold_max);
        ringMinSpin_->setValue(cfg.ring_ratio_min);
        ringMaxSpin_->setValue(cfg.ring_ratio_max);

        borderCheckBox_->setChecked(cfg.enable_border_check);
        areaRangeCheckBox_->setChecked(cfg.enable_area_range_check);
        deformRangeCheckBox_->setChecked(cfg.enable_deformability_range_check);
        areaRatioCheckBox_->setChecked(cfg.enable_area_ratio_check);
        ringRatioCheckBox_->setChecked(cfg.enable_ring_ratio_check);
        singleInnerCheckBox_->setChecked(cfg.require_single_inner_contour);

        // Sync histogram axis defaults from ring ratio config
        histogramXMin_ = cfg.ring_ratio_min;
        histogramXMax_ = cfg.ring_ratio_max;

        targetGroupEnableBox_->setChecked(cfg.enable_target_group);
        targetAreaMinSpin_->setValue(cfg.target_group_area_min);
        targetAreaMaxSpin_->setValue(cfg.target_group_area_max);
        targetDeformMinSpin_->setValue(cfg.target_group_deformability_min);
        targetDeformMaxSpin_->setValue(cfg.target_group_deformability_max);

        multiImageEnableBox_->setChecked(cfg.multi_image_enabled);
        multiImageCountSpin_->setValue(std::max(1, cfg.multi_image_count));
    }

    void ExperimentMonitoringTab::onApplyParams()
    {
        auto cfg = backend_.processing().getProcessingConfig();

        // Filter thresholds
        cfg.area_threshold_min = areaMinSpin_->value();
        cfg.area_threshold_max = areaMaxSpin_->value();
        cfg.deformability_threshold_min = deformMinSpin_->value();
        cfg.deformability_threshold_max = deformMaxSpin_->value();
        cfg.area_ratio_threshold_max = areaRatioMaxSpin_->value();
        cfg.ring_ratio_min = ringMinSpin_->value();
        cfg.ring_ratio_max = ringMaxSpin_->value();

        // Filter enables
        cfg.enable_border_check = borderCheckBox_->isChecked();
        cfg.enable_area_range_check = areaRangeCheckBox_->isChecked();
        cfg.enable_deformability_range_check = deformRangeCheckBox_->isChecked();
        cfg.enable_area_ratio_check = areaRatioCheckBox_->isChecked();
        cfg.enable_ring_ratio_check = ringRatioCheckBox_->isChecked();
        cfg.require_single_inner_contour = singleInnerCheckBox_->isChecked();

        // Target group
        cfg.enable_target_group = targetGroupEnableBox_->isChecked();
        cfg.target_group_area_min = targetAreaMinSpin_->value();
        cfg.target_group_area_max = targetAreaMaxSpin_->value();
        cfg.target_group_deformability_min = targetDeformMinSpin_->value();
        cfg.target_group_deformability_max = targetDeformMaxSpin_->value();

        // Multi-image acquisition
        cfg.multi_image_enabled = multiImageEnableBox_->isChecked();
        cfg.multi_image_count = multiImageCountSpin_->value();

        backend_.processing().setProcessingConfig(cfg);

        SPDLOG_INFO("Tune panel: applied config (area=[{},{}], deform=[{:.2f},{:.2f}], "
                     "ring=[{:.1f},{:.1f}], border={}, areaRange={}, deformRange={}, "
                     "areaRatio={}, ringRatio={}, singleInner={}, targetGroup={}, "
                     "multiImage={}/{} )",
                     cfg.area_threshold_min, cfg.area_threshold_max,
                     cfg.deformability_threshold_min, cfg.deformability_threshold_max,
                     cfg.ring_ratio_min, cfg.ring_ratio_max,
                     cfg.enable_border_check, cfg.enable_area_range_check,
                     cfg.enable_deformability_range_check, cfg.enable_area_ratio_check,
                     cfg.enable_ring_ratio_check,
                     cfg.require_single_inner_contour, cfg.enable_target_group,
                     cfg.multi_image_enabled, cfg.multi_image_count);

        emit processingConfigApplied();
    }

    void ExperimentMonitoringTab::setupCharts() {
        // Panel 1: Scatterplot (top-left)
        scatterplotChart_ = new QChart();
        scatterSeries_ = new QScatterSeries();
        scatterSeries_->setMarkerSize(6.0);
        scatterSeries_->setName("Valid Frames");
        scatterSeries_->setColor(QColor(0, 200, 0));
        scatterplotChart_->addSeries(scatterSeries_);

        targetGroupSeries_ = new QScatterSeries();
        targetGroupSeries_->setMarkerSize(6.0);
        targetGroupSeries_->setName("Target Group");
        targetGroupSeries_->setColor(QColor(0, 120, 255));
        scatterplotChart_->addSeries(targetGroupSeries_);
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
        targetGroupSeries_->attachAxis(scatterXAxis_);
        targetGroupSeries_->attachAxis(scatterYAxis_);
        scatterXAxis_->setRange(scatterXMin_, scatterXMax_);
        scatterYAxis_->setRange(scatterYMin_, scatterYMax_);

        scatterplotView_ = new ZoomableChartView(scatterplotChart_);
        scatterplotView_->setRenderHint(QPainter::Antialiasing);
        scatterplotView_->setDefaultRange(scatterXAxis_, scatterXMin_, scatterXMax_);
        scatterplotView_->setDefaultRange(scatterYAxis_, scatterYMin_, scatterYMax_);
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

        histogramView_ = new ZoomableChartView(histogramChart_);
        histogramView_->setRenderHint(QPainter::Antialiasing);
        if (histogramXAxis_)
            histogramView_->setDefaultRange(histogramXAxis_, histogramXMin_, histogramXMax_);
        histogramView_->setDefaultRange(histogramYAxis_, 0, std::max(1.0, histogramYMax_));
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
        // Refresh tune panel with current config when tab becomes visible
        loadCurrentConfig();
    }

    void ExperimentMonitoringTab::hideEvent(QHideEvent *event)
    {
        QWidget::hideEvent(event);
        if (updateTimer_ && updateTimer_->isActive())
        {
            updateTimer_->stop();
        }
        // Disarm periodic test trigger on hide to avoid background pulsing
        if (ui->periodicTriggerBtn->isChecked())
        {
            ui->periodicTriggerBtn->setChecked(false);
        }
    }


    void ExperimentMonitoringTab::updateScatterplot(const std::vector<backend::services::ProcessedFrame> &validFrames)
    {
        scatterSeries_->clear();
        targetGroupSeries_->clear();

        const double conversionFactor = backend_.processing().getPixelToMicronFactor();
        const double areaConversionFactor = conversionFactor * conversionFactor;

        for (const auto &frame : validFrames)
        {
            if (frame.validation.isValid)
            {
                double areaMicrons = frame.validation.area * areaConversionFactor;
                double deform = frame.validation.deformability;
                if (frame.validation.isTargetGroup) {
                    targetGroupSeries_->append(areaMicrons, deform);
                } else {
                    scatterSeries_->append(areaMicrons, deform);
                }
            }
        }

        if (!scatterplotView_->isUserZoomed()) {
            scatterXAxis_->setRange(scatterXMin_, scatterXMax_);
            scatterYAxis_->setRange(scatterYMin_, scatterYMax_);
        }
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

        if (!histogramView_->isUserZoomed()) {
            if (histogramXAxis_)
            {
                histogramXAxis_->setRange(minVal, maxVal);
                histogramXAxis_->setTickCount(6);
            }
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

        if (!histogramView_->isUserZoomed()) {
            const double yMax = std::max(1.0, histogramYMax_);
            histogramYAxis_->setRange(0, yMax);
        }

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
                roiImage = createOverlayImage(roiOriginal, roiMask, &frame.validation);
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

        // Fetch config once for reason derivation
        auto config = backend_.processing().getProcessingConfig();

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
                roiImage = createOverlayImage(roiOriginal, roiMask, &frame.validation);
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

                // Derive rejection reasons
                auto reasons = getInvalidReasons(frame.validation, config,
                    backend_.processing().getPixelToMicronFactor());

                // Container widget: image on top, reason text below
                QWidget *container = new QWidget(ui->invalidFramesWidget);
                QVBoxLayout *vbox = new QVBoxLayout(container);
                vbox->setContentsMargins(0, 0, 0, 0);
                vbox->setSpacing(2);

                QLabel *imageLabel = new QLabel(container);
                imageLabel->setPixmap(pixmap);
                imageLabel->setAlignment(Qt::AlignCenter);
                imageLabel->setFrameStyle(QFrame::Box);
                imageLabel->setLineWidth(1);
                imageLabel->setStyleSheet("QLabel { border: 1px solid gray; }");
                imageLabel->setFixedSize(THUMBNAIL_SIZE, THUMBNAIL_SIZE);
                imageLabel->setScaledContents(false);
                vbox->addWidget(imageLabel, 0, Qt::AlignCenter);

                if (!reasons.empty())
                {
                    QStringList shortReasons;
                    QStringList tooltipLines;
                    for (const auto &r : reasons)
                    {
                        shortReasons << r.shortText;
                        tooltipLines << r.longText;
                    }

                    QLabel *reasonLabel = new QLabel(shortReasons.join(" | "), container);
                    reasonLabel->setAlignment(Qt::AlignCenter);
                    reasonLabel->setWordWrap(true);
                    reasonLabel->setStyleSheet("QLabel { font-size: 9px; color: #cc0000; }");
                    reasonLabel->setFixedWidth(THUMBNAIL_SIZE);
                    vbox->addWidget(reasonLabel, 0, Qt::AlignCenter);

                    container->setToolTip(tooltipLines.join("\n"));
                }

                ui->invalidFramesGrid->addWidget(container, row, col);

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

    void ExperimentMonitoringTab::onSortTrigger()
    {
        backend_.trigger().onTargetGroupResult(true);
        SPDLOG_INFO("Manual sort trigger fired");
    }

    void ExperimentMonitoringTab::onPeriodicTriggerToggled(bool checked)
    {
        if (!periodicTriggerTimer_) return;
        if (checked)
        {
            const int intervalMs = ui->periodicTriggerIntervalSpin->value();
            periodicTriggerPulseCount_ = 0;
            periodicTriggerTimer_->setInterval(intervalMs);
            periodicTriggerTimer_->start();
            ui->periodicTriggerIntervalSpin->setEnabled(false);
            SPDLOG_INFO("Periodic sort trigger started (interval={} ms)", intervalMs);
        }
        else
        {
            periodicTriggerTimer_->stop();
            ui->periodicTriggerIntervalSpin->setEnabled(true);
            SPDLOG_INFO("Periodic sort trigger stopped (pulses fired={})", periodicTriggerPulseCount_);
        }
    }

    QImage ExperimentMonitoringTab::createOverlayImage(const cv::Mat &original, const cv::Mat &mask,
                                                       const backend::services::FilterResult *validation) const
    {
        if (original.empty() || mask.empty())
        {
            return QImage();
        }

        // Classification color: blue=target, green=valid, red=invalid
        cv::Vec3b tint(0, 255, 0); // default green
        if (validation) {
            if (validation->isTargetGroup)      tint = {0, 120, 255};  // Blue
            else if (validation->isValid)       tint = {0, 255, 0};    // Green
            else                                tint = {255, 0, 0};    // Red
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

        // Extract contours with hierarchy to isolate nested (inner) contours.
        // Inner contours (used for metrics) have a parent in the hierarchy.
        std::vector<std::vector<cv::Point>> contours;
        std::vector<cv::Vec4i> hierarchy;
        cv::findContours(mask.clone(), contours, hierarchy, cv::RETR_CCOMP, cv::CHAIN_APPROX_SIMPLE);

        // Build a mask containing only the inner (nested) contour regions
        cv::Mat innerMask = cv::Mat::zeros(mask.rows, mask.cols, CV_8UC1);
        bool hasInner = false;
        for (int i = 0; i < static_cast<int>(contours.size()); ++i) {
            if (hierarchy[i][3] >= 0) { // has parent → inner contour
                cv::drawContours(innerMask, contours, i, cv::Scalar(255), -1);
                hasInner = true;
            }
        }
        // Fallback: if no nested contour found, use the full mask
        const cv::Mat &tintMask = hasInner ? innerMask : mask;

        // Create overlay: colored tint where tintMask is non-zero (inner contour only)
        cv::Mat overlay = rgb.clone();
        for (int y = 0; y < overlay.rows && y < tintMask.rows; ++y)
        {
            for (int x = 0; x < overlay.cols && x < tintMask.cols; ++x)
            {
                if (tintMask.at<uchar>(y, x) > 0)
                {
                    cv::Vec3b &pixel = overlay.at<cv::Vec3b>(y, x);
                    pixel[0] = static_cast<uchar>(std::min(255.0, pixel[0] * 0.7 + tint[0] * 0.3));
                    pixel[1] = static_cast<uchar>(std::min(255.0, pixel[1] * 0.7 + tint[1] * 0.3));
                    pixel[2] = static_cast<uchar>(std::min(255.0, pixel[2] * 0.7 + tint[2] * 0.3));
                }
            }
        }

        // Draw contour outlines for both outer and inner contours
        const cv::Scalar contourColor(tint[0], tint[1], tint[2]);
        cv::drawContours(overlay, contours, -1, contourColor, 1);

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
        if (scatterplotView_) {
            scatterplotView_->setDefaultRange(scatterXAxis_, minVal, maxVal);
            scatterplotView_->resetZoom();
        }
    }

    void ExperimentMonitoringTab::setScatterYRange(double minVal, double maxVal)
    {
        if (minVal >= maxVal)
            return;
        scatterYMin_ = minVal;
        scatterYMax_ = maxVal;
        if (scatterplotView_) {
            scatterplotView_->setDefaultRange(scatterYAxis_, minVal, maxVal);
            scatterplotView_->resetZoom();
        }
    }

    void ExperimentMonitoringTab::setHistogramXRange(double minVal, double maxVal)
    {
        if (minVal >= maxVal)
            return;
        histogramXMin_ = minVal;
        histogramXMax_ = maxVal;
        if (histogramView_ && histogramXAxis_) {
            histogramView_->setDefaultRange(histogramXAxis_, minVal, maxVal);
            histogramView_->resetZoom();
        }
    }

    void ExperimentMonitoringTab::setHistogramYMax(double maxVal)
    {
        if (maxVal <= 0)
            return;
        histogramYMax_ = maxVal;
        if (histogramView_) {
            histogramView_->setDefaultRange(histogramYAxis_, 0, std::max(1.0, maxVal));
            histogramView_->resetZoom();
        }
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

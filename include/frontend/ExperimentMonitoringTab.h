#pragma once

#include <QWidget>
#include <QImage>
#include <vector>
#include <cstdint>

namespace cv { class Mat; }
namespace backend { class AppBackend; }
namespace backend::services { struct ProcessedFrame; }

class QTimer;
class QChartView;
class QScatterSeries;
class QLineSeries;
class QBarSeries;
class QBarSet;
class QHistogramSeries;
class QBarCategoryAxis;
class QChart;
class QValueAxis;
class QScrollArea;
class QGridLayout;
class QWidget;
class QLabel;
class QCheckBox;
class QPushButton;
class QHBoxLayout;
class QVBoxLayout;
class QShowEvent;
class QHideEvent;

namespace frontend {

class ExperimentMonitoringTab : public QWidget {
    Q_OBJECT
public:
    explicit ExperimentMonitoringTab(backend::AppBackend& backend, QWidget* parent = nullptr);
    ~ExperimentMonitoringTab() override;
    
    // Settings accessors
    double getKdeBandwidth() const { return kdeBandwidth_; }
    int getKdeGridResolution() const { return kdeGridResolution_; }
    void setKdeBandwidth(double bandwidth);
    void setKdeGridResolution(int resolution);

private slots:
    void onUpdate();
    void onToggleOverlay(bool enabled);
    void onClearBuffer();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void loadIsoelasticCurves();
    void updateScatterplot(const std::vector<backend::services::ProcessedFrame>& validFrames);
    void updateHistogram(const std::vector<backend::services::ProcessedFrame>& validFrames);
    void updateValidFramesGrid(const std::vector<backend::services::ProcessedFrame>& validFrames);
    void updateInvalidFramesGrid(const std::vector<backend::services::ProcessedFrame>& invalidFrames);
    QImage extractRoiImage(const cv::Mat& image, int x, int y, int w, int h) const;
    QImage matToQImage(const cv::Mat& mat) const;
    void clearGrid(QGridLayout* grid);
    QImage createOverlayImage(const cv::Mat& original, const cv::Mat& mask) const;
    std::vector<std::vector<double>> computeKDE(const std::vector<std::pair<double, double>>& points, 
                                                 int gridX, int gridY, double bandwidth) const;

    backend::AppBackend& backend_;
    QTimer* updateTimer_ = nullptr;

    // Panel 1: Scatterplot
    QChartView* scatterplotView_ = nullptr;
    QChart* scatterplotChart_ = nullptr;
    QScatterSeries* scatterSeries_ = nullptr;
    QValueAxis* scatterXAxis_ = nullptr;
    QValueAxis* scatterYAxis_ = nullptr;
    std::vector<QLineSeries*> isoelasticCurves_;

    // Panel 2: Histogram
    QChartView* histogramView_ = nullptr;
    QChart* histogramChart_ = nullptr;
#ifndef MIB_HAS_QHISTOGRAMSERIES
#if __has_include(<QHistogramSeries>)
#define MIB_HAS_QHISTOGRAMSERIES 1
#else
#define MIB_HAS_QHISTOGRAMSERIES 0
#endif
#endif
#if MIB_HAS_QHISTOGRAMSERIES
    QHistogramSeries* histogramSeries_ = nullptr;
#else
    QBarSeries* barSeries_ = nullptr;
    QBarCategoryAxis* histogramCategoryAxis_ = nullptr;
#endif
    QValueAxis* histogramYAxis_ = nullptr;
    QValueAxis* histogramXAxis_ = nullptr;

    // Panel 3: Valid frames grid
    QWidget* validFramesContainer_ = nullptr;
    QVBoxLayout* validFramesLayout_ = nullptr;
    QHBoxLayout* validFramesControls_ = nullptr;
    QCheckBox* validOverlayCheck_ = nullptr;
    QScrollArea* validFramesScroll_ = nullptr;
    QWidget* validFramesWidget_ = nullptr;
    QGridLayout* validFramesGrid_ = nullptr;

    // Panel 4: Invalid frames grid
    QWidget* invalidFramesContainer_ = nullptr;
    QVBoxLayout* invalidFramesLayout_ = nullptr;
    QHBoxLayout* invalidFramesControls_ = nullptr;
    QCheckBox* invalidOverlayCheck_ = nullptr;
    QScrollArea* invalidFramesScroll_ = nullptr;
    QWidget* invalidFramesWidget_ = nullptr;
    QGridLayout* invalidFramesGrid_ = nullptr;
    
    // Controls
    QPushButton* clearBufferBtn_ = nullptr;
    
    // Overlay state
    bool showValidOverlay_ = false;
    bool showInvalidOverlay_ = false;
    
    // KDE settings
    double kdeBandwidth_ = 50.0;
    int kdeGridResolution_ = 50;

    // Rolling buffers to maintain recent frames even after flush
    std::vector<backend::services::ProcessedFrame> recentValidFrames_;
    std::vector<backend::services::ProcessedFrame> recentInvalidFrames_;
    uint64_t lastValidFrameIndex_ = 0;
    uint64_t lastInvalidFrameIndex_ = 0;

    static constexpr int THUMBNAIL_SIZE = 100;
    static constexpr int GRID_COLUMNS = 5;
    static constexpr int MAX_FRAMES_TO_SHOW = 25;
    static constexpr int MAX_RECENT_FRAMES = 1000; // Keep more frames for scatterplot/histogram
    static constexpr int UPDATE_INTERVAL_MS = 500;
    static constexpr double HISTOGRAM_BIN_WIDTH = 0.5;
    static constexpr double HISTOGRAM_MIN = 15.0;
    static constexpr double HISTOGRAM_MAX = 25.0;
    static constexpr int HISTOGRAM_BINS = static_cast<int>((HISTOGRAM_MAX - HISTOGRAM_MIN) / HISTOGRAM_BIN_WIDTH);
};

} // namespace frontend


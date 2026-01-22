#pragma once

#include <QWidget>
#include <QImage>
#include <QCache>
#include <vector>
#include <cstdint>
#include <memory>

namespace cv
{
    class Mat;
}
namespace backend
{
    class AppBackend;
}
namespace backend::services
{
    struct ProcessedFrame;
}

#include "backend/services/ProcessingService.h"

class QPushButton;
class QLabel;
class QTabWidget;
class QTableView;
class QGridLayout;
class QScrollArea;
class QVBoxLayout;
class QHBoxLayout;
class QCheckBox;
class QSpacerItem;
class QChartView;
class QChart;
class QScatterSeries;
class QLineSeries;
class QValueAxis;
#if __has_include(<QHistogramSeries>)
class QHistogramSeries;
#else
class QBarSeries;
class QBarCategoryAxis;
#endif

namespace frontend
{
    class HdfMetricsModel;
}

namespace Ui { class HdfReviewTab; }

namespace frontend
{

    class HdfReviewTab : public QWidget
    {
        Q_OBJECT
    public:
        explicit HdfReviewTab(backend::AppBackend &backend, QWidget *parent = nullptr);
        ~HdfReviewTab() override;

    private slots:
        void onSelectFile();
        void onExportMetrics();
        void onExportAll();
        void onExportCharts();
        void onToggleRoiOverlay(bool enabled);
        void onTabChanged(int index);
        void onThumbnailClicked(int frameIndex);
        void onThumbnailDoubleClicked(int frameIndex);
        void onTableSelectionChanged();
        void onViewFrameDetails(int frameIndex);

    private:
        void setupCharts();
        void loadHdfFile(const QString &filePath);
        void populateFrames(const std::vector<backend::services::ProcessedFrame> &frames, bool isValid);
        void clearDisplay();
        void updateImageGrid(const std::vector<backend::services::ProcessedFrame> &frames);
        void updateMetricsTable(const std::vector<backend::services::ProcessedFrame> &frames);
        void loadThumbnailsBatch(const std::vector<backend::services::ProcessedFrame> &frames,
                                 size_t startIndex, size_t count, bool isValid);
        QImage matToQImage(const cv::Mat &mat) const;
        void setSelectedFrame(int frameIndex);
        void onScrollValueChanged(int value);
        void exportMetricsToCsv(const QString &filePath);
        QImage drawRoiOverlay(const QImage &image, int imgWidth, int imgHeight) const;
        QImage createProcessingOverlay(const cv::Mat &original, const cv::Mat &mask) const;
        void showFrameViewer(int frameIndex);
        // Carousel/refresh helpers
        void computeVisibleRange(bool isValid, size_t &outStartIndex, size_t &outEndIndex) const;
        void refreshVisibleThumbnails(bool isValid);
        void pruneOffscreenThumbnails(bool isValid);
        QImage buildThumbnailForIndex(size_t index, bool isValid);
        void exportAllImagesToTiff(const QString& baseDir);
        bool exportChartFromHdf5(const std::string& datasetPath, const QString& filePath);
        void exportAllData(const QString& baseDir);
        void updateCharts();
        void generateScatterPlot(const std::vector<backend::services::ProcessedFrame>& validFrames);
        void generateHistogram(const std::vector<backend::services::ProcessedFrame>& validFrames);
        void loadIsoelasticCurves();
        QPixmap chartToPixmap(QChartView* chartView) const;

        Ui::HdfReviewTab* ui;
        backend::AppBackend &backend_;
        std::unique_ptr<backend::services::Hdf5Service> hdfReader_;

        // Charts (created in C++)
        QChartView *scatterPlotView_ = nullptr;
        QChart *scatterPlotChart_ = nullptr;
        QScatterSeries *scatterSeries_ = nullptr;
        QValueAxis *scatterXAxis_ = nullptr;
        QValueAxis *scatterYAxis_ = nullptr;
        std::vector<QLineSeries*> isoelasticCurves_;
        QChartView *histogramView_ = nullptr;
        QChart *histogramChart_ = nullptr;
#if __has_include(<QHistogramSeries>)
        QHistogramSeries *histogramSeries_ = nullptr;
#else
        QBarSeries *histogramBarSeries_ = nullptr;
        QBarCategoryAxis *histogramCategoryAxis_ = nullptr;
#endif
        QValueAxis *histogramXAxis_ = nullptr;
        QValueAxis *histogramYAxis_ = nullptr;

        // Models
        HdfMetricsModel *validMetricsModel_ = nullptr;
        HdfMetricsModel *invalidMetricsModel_ = nullptr;

        // Spacers for grid layout
        QSpacerItem *validBottomSpacer_ = nullptr;
        QSpacerItem *validTopSpacer_ = nullptr;
        QSpacerItem *invalidBottomSpacer_ = nullptr;
        QSpacerItem *invalidTopSpacer_ = nullptr;

        // Data
        std::vector<backend::services::ProcessedFrame> validFrames_;
        std::vector<backend::services::ProcessedFrame> invalidFrames_;
        int selectedFrameIndex_ = -1;
        bool isShowingValid_ = true;
        bool showRoiOverlay_ = false;
        backend::services::ProcessingService::Roi roi_{0, 0, 0, 0};

        static constexpr int THUMBNAIL_SIZE = 128;
        static constexpr int GRID_COLUMNS = 5;
        static constexpr size_t INITIAL_THUMBNAIL_COUNT = 200; // Load first 200 thumbnails
        static constexpr size_t BATCH_THUMBNAIL_COUNT = 100;   // Load 100 more when scrolling

        size_t validThumbnailsLoaded_ = 0;
        size_t invalidThumbnailsLoaded_ = 0;

        // Thumbnail cache (key encodes frame type + index)
        QCache<qulonglong, QImage> thumbnailCache_;

        // Preserve scroll positions per tab
        int validScrollValue_ = 0;
        int invalidScrollValue_ = 0;
    };

} // namespace frontend

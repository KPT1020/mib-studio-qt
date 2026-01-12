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

namespace frontend
{
    class HdfMetricsModel;
}

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

        backend::AppBackend &backend_;
        std::unique_ptr<backend::services::Hdf5Service> hdfReader_;

        // UI components
        QPushButton *selectFileBtn_ = nullptr;
        QPushButton *exportMetricsBtn_ = nullptr;
        QPushButton *exportAllBtn_ = nullptr;
        QPushButton *exportChartsBtn_ = nullptr;
        QCheckBox *roiOverlayCheck_ = nullptr;
        QLabel *filePathLabel_ = nullptr;
        QLabel *statusLabel_ = nullptr;
        QTabWidget *frameTypeTabs_ = nullptr;

        // Valid frames tab
        QWidget *validFramesWidget_ = nullptr;
        QScrollArea *validImageScroll_ = nullptr;
        QWidget *validImageGridWidget_ = nullptr;
        QGridLayout *validImageGrid_ = nullptr;
        QTableView *validMetricsTable_ = nullptr;
        HdfMetricsModel *validMetricsModel_ = nullptr;
        QSpacerItem *validBottomSpacer_ = nullptr;
        QSpacerItem *validTopSpacer_ = nullptr;

        // Invalid frames tab
        QWidget *invalidFramesWidget_ = nullptr;
        QScrollArea *invalidImageScroll_ = nullptr;
        QWidget *invalidImageGridWidget_ = nullptr;
        QGridLayout *invalidImageGrid_ = nullptr;
        QTableView *invalidMetricsTable_ = nullptr;
        HdfMetricsModel *invalidMetricsModel_ = nullptr;
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

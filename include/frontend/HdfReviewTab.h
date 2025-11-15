#pragma once

#include <QWidget>
#include <QImage>
#include <vector>
#include <cstdint>

namespace cv { class Mat; }
namespace backend { class AppBackend; }
namespace backend::services { struct ProcessedFrame; }

#include "backend/services/ProcessingService.h"

class QPushButton;
class QLabel;
class QTabWidget;
class QTableWidget;
class QGridLayout;
class QScrollArea;
class QVBoxLayout;
class QHBoxLayout;
class QCheckBox;

namespace frontend {

class HdfReviewTab : public QWidget {
    Q_OBJECT
public:
    explicit HdfReviewTab(backend::AppBackend& backend, QWidget* parent = nullptr);
    ~HdfReviewTab() override;

private slots:
    void onSelectFile();
    void onExportMetrics();
    void onToggleRoiOverlay(bool enabled);
    void onTabChanged(int index);
    void onThumbnailClicked(int frameIndex);
    void onThumbnailDoubleClicked(int frameIndex);
    void onTableSelectionChanged();
    void onViewFrameDetails(int frameIndex);

private:
    void loadHdfFile(const QString& filePath);
    void populateFrames(const std::vector<backend::services::ProcessedFrame>& frames, bool isValid);
    void clearDisplay();
    void updateImageGrid(const std::vector<backend::services::ProcessedFrame>& frames);
    void updateMetricsTable(const std::vector<backend::services::ProcessedFrame>& frames);
    void loadThumbnailsBatch(const std::vector<backend::services::ProcessedFrame>& frames, 
                             size_t startIndex, size_t count, bool isValid);
    QImage matToQImage(const cv::Mat& mat) const;
    void setSelectedFrame(int frameIndex);
    void onScrollValueChanged(int value);
    void exportMetricsToCsv(const QString& filePath);
    QImage drawRoiOverlay(const QImage& image, int imgWidth, int imgHeight) const;
    QImage createProcessingOverlay(const cv::Mat& original, const cv::Mat& mask) const;
    void showFrameViewer(int frameIndex);

    backend::AppBackend& backend_;

    // UI components
    QPushButton* selectFileBtn_ = nullptr;
    QPushButton* exportMetricsBtn_ = nullptr;
    QCheckBox* roiOverlayCheck_ = nullptr;
    QLabel* filePathLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QTabWidget* frameTypeTabs_ = nullptr;
    
    // Valid frames tab
    QWidget* validFramesWidget_ = nullptr;
    QScrollArea* validImageScroll_ = nullptr;
    QWidget* validImageGridWidget_ = nullptr;
    QGridLayout* validImageGrid_ = nullptr;
    QTableWidget* validMetricsTable_ = nullptr;
    
    // Invalid frames tab
    QWidget* invalidFramesWidget_ = nullptr;
    QScrollArea* invalidImageScroll_ = nullptr;
    QWidget* invalidImageGridWidget_ = nullptr;
    QGridLayout* invalidImageGrid_ = nullptr;
    QTableWidget* invalidMetricsTable_ = nullptr;

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
    static constexpr size_t BATCH_THUMBNAIL_COUNT = 100;  // Load 100 more when scrolling
    
    size_t validThumbnailsLoaded_ = 0;
    size_t invalidThumbnailsLoaded_ = 0;
};

} // namespace frontend


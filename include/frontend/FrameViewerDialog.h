#pragma once

#include <QDialog>
#include <QImage>
#include <cstdint>

namespace cv { class Mat; }
namespace backend::services { 
    struct ProcessedFrame;
}
#include "backend/services/ProcessingService.h"

class QLabel;
class QPushButton;
class QCheckBox;
class QScrollArea;
class QVBoxLayout;
class QHBoxLayout;
class QGridLayout;

namespace frontend {

class FrameViewerDialog : public QDialog {
    Q_OBJECT
public:
    explicit FrameViewerDialog(const backend::services::ProcessedFrame& frame,
                              const backend::services::ProcessingService::Roi& roi,
                              bool showOverlays,
                              QWidget* parent = nullptr);
    
    void setFrame(const backend::services::ProcessedFrame& frame);
    void setRoi(const backend::services::ProcessingService::Roi& roi);
    void setShowOverlays(bool show);

private slots:
    void onToggleProcessingOverlay(bool enabled);
    void onToggleRoiOverlay(bool enabled);
    void onPreviousFrame();
    void onNextFrame();
    void onZoomIn();
    void onZoomOut();
    void onFitToWindow();
    void onExportFrame();

signals:
    void requestPreviousFrame();
    void requestNextFrame();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void updateImage();
    QImage createProcessingOverlay(const cv::Mat& original, const cv::Mat& mask) const;
    QImage matToQImage(const cv::Mat& mat) const;
    void updateFrameInfo();

    const backend::services::ProcessedFrame* frame_;
    backend::services::ProcessingService::Roi roi_;
    bool showProcessingOverlay_;
    bool showRoiOverlay_;
    
    QImage displayImage_;
    double zoomFactor_;
    
    // UI components
    QLabel* frameInfoLabel_;
    QScrollArea* imageScrollArea_;
    QLabel* imageLabel_;
    QPushButton* prevButton_;
    QPushButton* nextButton_;
    QCheckBox* processingOverlayCheck_;
    QCheckBox* roiOverlayCheck_;
    QPushButton* zoomInButton_;
    QPushButton* zoomOutButton_;
    QPushButton* fitToWindowButton_;
    QPushButton* exportButton_;
    QPushButton* closeButton_;
};

} // namespace frontend


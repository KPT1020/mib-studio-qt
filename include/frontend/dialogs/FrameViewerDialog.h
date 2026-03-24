#pragma once

#include <QDialog>
#include <QImage>
#include <cstdint>

namespace cv { class Mat; }
namespace backend::services { 
    struct ProcessedFrame;
}
#include "backend/services/ProcessingService.h"
#include "frontend/utils/OverlayRenderer.h"

class QLabel;
class QComboBox;
class QPushButton;
class QCheckBox;
class QScrollArea;
class QVBoxLayout;
class QHBoxLayout;
class QGridLayout;

namespace Ui { class FrameViewerDialog; }

namespace frontend {

class FrameViewerDialog : public QDialog {
    Q_OBJECT
public:
    explicit FrameViewerDialog(const backend::services::ProcessedFrame& frame,
                              const backend::services::ProcessingService::Roi& roi,
                              OverlayMode overlayMode,
                              bool showRoiOverlay,
                              QWidget* parent = nullptr);
    ~FrameViewerDialog();
    
    void setFrame(const backend::services::ProcessedFrame& frame);
    void setRoi(const backend::services::ProcessingService::Roi& roi);
    void setOverlayMode(OverlayMode mode);
    void setShowRoiOverlay(bool show);

private slots:
    void onOverlayModeChanged(int index);
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
    QImage matToQImage(const cv::Mat& mat) const;
    void updateFrameInfo();

    const backend::services::ProcessedFrame* frame_;
    backend::services::ProcessingService::Roi roi_;
    OverlayMode overlayMode_;
    bool showRoiOverlay_;
    
    QImage displayImage_;
    double zoomFactor_;
    
    Ui::FrameViewerDialog* ui;
};

} // namespace frontend


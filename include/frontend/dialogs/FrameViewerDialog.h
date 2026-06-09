#pragma once

#include <QDialog>
#include <QImage>
#include <cstdint>

namespace cv { class Mat; }
namespace backend::services { 
    struct ProcessedFrame;
}
#include "backend/processing/ProcessingService.h"
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
    void onPreviousSeriesImage();
    void onNextSeriesImage();
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
    void updateSeriesControls();

    backend::services::ProcessedFrame frame_;
    backend::services::ProcessingService::Roi roi_;
    OverlayMode overlayMode_;
    bool showRoiOverlay_;

    QImage displayImage_;
    double zoomFactor_;
    int seriesImageIndex_; // 0 = trigger image, 1..N-1 = subsequent series images

    // Series navigation widgets (created programmatically)
    QLabel* seriesLabel_;
    QPushButton* seriesPrevBtn_;
    QPushButton* seriesNextBtn_;

    Ui::FrameViewerDialog* ui;
};

} // namespace frontend


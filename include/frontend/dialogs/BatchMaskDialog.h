#pragma once

#include <QDialog>
#include <QImage>
#include <QRect>
#include <QString>

#include <memory>
#include <vector>

#include "backend/services/ProcessingService.h"
#include "frontend/utils/RoiDrawCanvas.h"

namespace cv { class Mat; }
namespace backend { class AppBackend; }

class QRadioButton;
class QLineEdit;
class QSpinBox;
class QPushButton;
class QLabel;
class QProgressBar;
class QPlainTextEdit;
class QDoubleSpinBox;

namespace frontend {

// Dialog for running the batch mask generation pipeline on a range of
// stream images sourced from either an HDF5 file or a folder. On
// completion the results are written to a standard HDF5 file next to
// the source; the saved path is exposed via savedHdf5Path() so the
// caller can reload HdfReviewTab from it.
//
// The right-hand preview panel lets the user visually select an ROI by
// dragging on a source frame, and designate one frame as the background
// image for subtraction. These override the live pipeline values.
class BatchMaskDialog : public QDialog {
    Q_OBJECT
public:
    // `hdf5LoadedPath` is the path of the currently open HDF5 file (if any).
    // When non-empty, the "Current HDF5 frames" radio becomes available.
    explicit BatchMaskDialog(backend::AppBackend& backend,
                             QString hdf5LoadedPath = {},
                             QWidget* parent = nullptr);
    ~BatchMaskDialog() override;

    // After the dialog closes with Accepted:
    // - processedFrames() holds the raw batch results (always populated on success)
    // - savedHdf5Path() holds the path of the written HDF5 file (empty if save failed)
    const std::vector<backend::services::ProcessedFrame>& processedFrames() const { return results_; }
    QString savedHdf5Path() const { return savedHdf5Path_; }

private slots:
    void onSourceChanged();
    void onBrowseFolder();
    void onRun();

    void onPreviewSourceChanged();
    void onPrevFrame();
    void onNextFrame();
    void onSetBackground();
    void onClearBackground();
    void resetConfigToLive();

private:
    void buildUi();
    bool loadInputs(std::vector<cv::Mat>& outGray,
                    std::vector<std::string>& outNames,
                    QString& errorOut);
    void setRunning(bool running);

    void    loadPreviewFrame(int index);
    int     getSourceFrameCount() const;
    QImage  matToQImage(const cv::Mat& gray) const;
    QString computeAutoOutputPath() const;

    backend::AppBackend& backend_;
    QString hdf5LoadedPath_;

    // Source selection
    QRadioButton* srcHdf5_ = nullptr;
    QRadioButton* srcFolder_ = nullptr;
    QLineEdit* folderEdit_ = nullptr;
    QPushButton* folderBrowseBtn_ = nullptr;
    QSpinBox* startIdxSpin_ = nullptr;
    QSpinBox* countSpin_ = nullptr;

    // Status + controls
    QPushButton* runBtn_ = nullptr;
    QPushButton* closeBtn_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QPlainTextEdit* logView_ = nullptr;

    // Preview panel
    RoiDrawCanvas* roiCanvas_        = nullptr;
    QLabel*        frameCountLabel_  = nullptr;
    QPushButton*   prevFrameBtn_     = nullptr;
    QPushButton*   nextFrameBtn_     = nullptr;
    QPushButton*   setBgBtn_         = nullptr;
    QPushButton*   clearBgBtn_       = nullptr;
    QLabel*        bgStatusLabel_    = nullptr;

    // Preview state
    int     previewFrameIndex_ = 0;
    int     previewFrameTotal_ = 0;
    cv::Mat backgroundMat_;   // empty = no background subtraction

    // Config panel
    QSpinBox*       blurSpin_        = nullptr;
    QSpinBox*       bgThreshSpin_    = nullptr;
    QSpinBox*       morphKernelSpin_ = nullptr;
    QSpinBox*       morphIterSpin_   = nullptr;
    QSpinBox*       areaMinSpin_     = nullptr;
    QSpinBox*       areaMaxSpin_     = nullptr;
    QDoubleSpinBox* deformMinSpin_   = nullptr;
    QDoubleSpinBox* deformMaxSpin_   = nullptr;

    // Local config — scoped to this batch run, never written back to live pipeline
    backend::services::ProcessingConfig localConfig_;

    std::vector<backend::services::ProcessedFrame> results_;
    QString savedHdf5Path_;
};

} // namespace frontend

#pragma once

#include <QDialog>
#include <QString>

#include <memory>
#include <vector>

#include "backend/services/ProcessingService.h"

namespace backend { class AppBackend; }

class QRadioButton;
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QPushButton;
class QLabel;
class QProgressBar;
class QPlainTextEdit;

namespace frontend {

// Dialog for running the batch mask generation pipeline on a range of
// stream images sourced from either an HDF5 file or a folder. Results can
// be saved as PNG masks, written to a new HDF5 file, and/or returned via
// processedFrames() for display in the parent tab.
class BatchMaskDialog : public QDialog {
    Q_OBJECT
public:
    // `hdf5LoadedPath` is the path of the currently open HDF5 file (if any).
    // When non-empty, the "Current HDF5 frames" radio becomes available.
    explicit BatchMaskDialog(backend::AppBackend& backend,
                             QString hdf5LoadedPath = {},
                             QWidget* parent = nullptr);
    ~BatchMaskDialog() override;

    // After the dialog closes with Accepted, these return the results of
    // the most recent successful run. processedFrames() is only populated
    // when the "Display in review tab" checkbox was ticked.
    const std::vector<backend::services::ProcessedFrame>& processedFrames() const { return results_; }
    bool displayRequested() const;

private slots:
    void onSourceChanged();
    void onBrowseFolder();
    void onBrowseOutputPng();
    void onBrowseOutputHdf5();
    void onRun();

private:
    void buildUi();
    bool loadInputs(std::vector<cv::Mat>& outGray,
                    std::vector<std::string>& outNames,
                    QString& errorOut);
    void setRunning(bool running);

    backend::AppBackend& backend_;
    QString hdf5LoadedPath_;

    // Source selection
    QRadioButton* srcHdf5_ = nullptr;
    QRadioButton* srcFolder_ = nullptr;
    QLineEdit* folderEdit_ = nullptr;
    QPushButton* folderBrowseBtn_ = nullptr;
    QSpinBox* startIdxSpin_ = nullptr;
    QSpinBox* countSpin_ = nullptr;

    // Output options
    QCheckBox* displayCheck_ = nullptr;
    QCheckBox* savePngCheck_ = nullptr;
    QLineEdit* pngDirEdit_ = nullptr;
    QPushButton* pngBrowseBtn_ = nullptr;
    QCheckBox* saveHdf5Check_ = nullptr;
    QLineEdit* hdf5PathEdit_ = nullptr;
    QPushButton* hdf5BrowseBtn_ = nullptr;

    // Status + controls
    QPushButton* runBtn_ = nullptr;
    QPushButton* closeBtn_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QPlainTextEdit* logView_ = nullptr;

    std::vector<backend::services::ProcessedFrame> results_;
};

} // namespace frontend

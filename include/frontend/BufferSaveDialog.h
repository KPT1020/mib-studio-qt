#pragma once

#include <QDialog>

namespace backend { class AppBackend; }

class QLineEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QLabel;
class QGroupBox;
class QCheckBox;

namespace frontend {

class BufferSaveDialog : public QDialog {
    Q_OBJECT
public:
    explicit BufferSaveDialog(backend::AppBackend& backend, QWidget* parent = nullptr);

private slots:
    void onBrowseDirectory();
    void onRangeModeChanged();
    void onRefreshRanges();
    void onApplyResize();
    void onSaveFrames();
    void onDialogAccepted();

private:
    void updateAvailableRanges();
    void updateUIState();
    bool validateInputs();
    QString formatTimestamp(uint64_t timestampNs) const;
    void updateMemoryDisplay();
    QString formatMemoryBytes(uint64_t bytes) const;

    backend::AppBackend& backend_;

    // Output directory
    QLineEdit* outputDirEdit_ = nullptr;
    QPushButton* browseBtn_ = nullptr;

    // Range selection
    QGroupBox* rangeGroup_ = nullptr;
    QRadioButton* allFramesRadio_ = nullptr;
    QRadioButton* indexRangeRadio_ = nullptr;
    QRadioButton* timestampRangeRadio_ = nullptr;
    QSpinBox* startIndexSpin_ = nullptr;
    QSpinBox* endIndexSpin_ = nullptr;
    QSpinBox* startTimestampSpin_ = nullptr;
    QSpinBox* endTimestampSpin_ = nullptr;
    QLabel* availableRangeLabel_ = nullptr;
    QLabel* availableTimestampLabel_ = nullptr;
    QPushButton* refreshRangesBtn_ = nullptr;

    // Buffer size
    QGroupBox* bufferSizeGroup_ = nullptr;
    QLabel* currentCapacityLabel_ = nullptr;
    QSpinBox* newCapacitySpin_ = nullptr;
    QLabel* estimatedMemoryLabel_ = nullptr;
    QPushButton* applyResizeBtn_ = nullptr;

    // Filter options
    QCheckBox* filterEmptyFramesCheck_ = nullptr;

    // Status and buttons
    QLabel* statusLabel_ = nullptr;
    QPushButton* saveBtn_ = nullptr;
};

} // namespace frontend


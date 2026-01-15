#pragma once

#include <QDialog>

class QSpinBox;
class QDialogButtonBox;
class QCheckBox;
class PlaybackPanel;

namespace backend { class AppBackend; }

class ProcessingSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ProcessingSettingsDialog(backend::AppBackend& backend, PlaybackPanel* playbackPanel = nullptr, QWidget* parent = nullptr);

private slots:
    void onApply();

private:
    void applySettings();
    void updateRoiLimits();

    backend::AppBackend& backend_;
    PlaybackPanel* playbackPanel_{nullptr};
    QSpinBox* invalidSamplingSpin_{nullptr};
    QSpinBox* flushIntervalSpin_{nullptr};
    QSpinBox* roiXSpin_{nullptr};
    QSpinBox* roiYSpin_{nullptr};
    QSpinBox* roiWidthSpin_{nullptr};
    QSpinBox* roiHeightSpin_{nullptr};
    QCheckBox* dropFramesCheck_{nullptr};
    QDialogButtonBox* buttons_{nullptr};
};



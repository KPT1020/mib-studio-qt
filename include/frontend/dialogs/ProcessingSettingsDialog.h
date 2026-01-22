#pragma once

#include <QDialog>

class QSpinBox;
class QDialogButtonBox;
class QCheckBox;
class PlaybackPanel;

namespace backend { class AppBackend; }
namespace Ui { class ProcessingSettingsDialog; }

class ProcessingSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ProcessingSettingsDialog(backend::AppBackend& backend, PlaybackPanel* playbackPanel = nullptr, QWidget* parent = nullptr);
    ~ProcessingSettingsDialog();

private slots:
    void onApply();

private:
    void applySettings();
    void updateRoiLimits();

    Ui::ProcessingSettingsDialog* ui;
    backend::AppBackend& backend_;
    PlaybackPanel* playbackPanel_{nullptr};
};



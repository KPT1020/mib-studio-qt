#pragma once

#include <QDialog>

class QSpinBox;
class QDialogButtonBox;

namespace backend { class AppBackend; }

class ProcessingSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ProcessingSettingsDialog(backend::AppBackend& backend, QWidget* parent = nullptr);

private slots:
    void onApply();

private:
    void applySettings();

    backend::AppBackend& backend_;
    QSpinBox* invalidSamplingSpin_{nullptr};
    QSpinBox* flushIntervalSpin_{nullptr};
    QDialogButtonBox* buttons_{nullptr};
};



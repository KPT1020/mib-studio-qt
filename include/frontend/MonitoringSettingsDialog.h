#pragma once

#include <QDialog>

class QDoubleSpinBox;
class QSpinBox;
class QDialogButtonBox;

namespace frontend {
class ExperimentMonitoringTab;
}

class MonitoringSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit MonitoringSettingsDialog(frontend::ExperimentMonitoringTab* monitoringTab, QWidget* parent = nullptr);

private slots:
    void onApply();
    void onOk();

private:
    void applySettings();

    frontend::ExperimentMonitoringTab* monitoringTab_;
    QDoubleSpinBox* kdeBandwidthSpin_;
    QSpinBox* kdeGridResolutionSpin_;
    QDialogButtonBox* buttons_;
};


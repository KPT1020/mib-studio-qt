#pragma once

#include <QDialog>

class QDoubleSpinBox;
class QSpinBox;
class QDialogButtonBox;

namespace frontend {
class ExperimentMonitoringTab;
}
namespace Ui { class MonitoringSettingsDialog; }

class MonitoringSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit MonitoringSettingsDialog(frontend::ExperimentMonitoringTab* monitoringTab, QWidget* parent = nullptr);
    ~MonitoringSettingsDialog();

private slots:
    void onApply();
    void onOk();

private:
    void applySettings();

    Ui::MonitoringSettingsDialog* ui;
    frontend::ExperimentMonitoringTab* monitoringTab_;
};


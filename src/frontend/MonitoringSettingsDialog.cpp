#include "frontend/MonitoringSettingsDialog.h"
#include "ui_MonitoringSettingsDialog.h"
#include "frontend/ExperimentMonitoringTab.h"

#include <QPushButton>

#include <spdlog/spdlog.h>

MonitoringSettingsDialog::MonitoringSettingsDialog(frontend::ExperimentMonitoringTab* monitoringTab, QWidget* parent)
    : QDialog(parent), ui(new Ui::MonitoringSettingsDialog), monitoringTab_(monitoringTab) {
    ui->setupUi(this);

    // Load current values
    if (monitoringTab_) {
        ui->kdeBandwidthSpin->setValue(monitoringTab_->getKdeBandwidth());
        ui->kdeGridResolutionSpin->setValue(monitoringTab_->getKdeGridResolution());
    } else {
        ui->kdeBandwidthSpin->setValue(50.0);
        ui->kdeGridResolutionSpin->setValue(50);
    }

    connect(ui->buttons, &QDialogButtonBox::accepted, this, &MonitoringSettingsDialog::onOk);
    connect(ui->buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(ui->buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &MonitoringSettingsDialog::onApply);
}

MonitoringSettingsDialog::~MonitoringSettingsDialog() {
    delete ui;
}

void MonitoringSettingsDialog::onApply() {
    applySettings();
}

void MonitoringSettingsDialog::onOk() {
    applySettings();
    accept();
}

void MonitoringSettingsDialog::applySettings() {
    if (monitoringTab_) {
        monitoringTab_->setKdeBandwidth(ui->kdeBandwidthSpin->value());
        monitoringTab_->setKdeGridResolution(ui->kdeGridResolutionSpin->value());
        SPDLOG_INFO("Monitoring settings applied: KDE bandwidth={}, grid resolution={}",
                    ui->kdeBandwidthSpin->value(), ui->kdeGridResolutionSpin->value());
    }
}


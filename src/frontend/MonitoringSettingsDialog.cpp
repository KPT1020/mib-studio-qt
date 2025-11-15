#include "frontend/MonitoringSettingsDialog.h"
#include "frontend/ExperimentMonitoringTab.h"

#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QDialogButtonBox>
#include <QPushButton>

#include <spdlog/spdlog.h>

MonitoringSettingsDialog::MonitoringSettingsDialog(frontend::ExperimentMonitoringTab* monitoringTab, QWidget* parent)
    : QDialog(parent), monitoringTab_(monitoringTab) {
    setWindowTitle(tr("Monitoring Settings"));
    setModal(true);

    auto* layout = new QFormLayout(this);

    kdeBandwidthSpin_ = new QDoubleSpinBox(this);
    kdeBandwidthSpin_->setMinimum(1.0);
    kdeBandwidthSpin_->setMaximum(1000.0);
    kdeBandwidthSpin_->setSingleStep(10.0);
    kdeBandwidthSpin_->setDecimals(1);
    kdeBandwidthSpin_->setToolTip(tr("KDE bandwidth parameter (higher = smoother, lower = more detailed)"));

    kdeGridResolutionSpin_ = new QSpinBox(this);
    kdeGridResolutionSpin_->setMinimum(10);
    kdeGridResolutionSpin_->setMaximum(200);
    kdeGridResolutionSpin_->setSingleStep(10);
    kdeGridResolutionSpin_->setToolTip(tr("Grid resolution for KDE heat map (higher = more detailed but slower)"));

    layout->addRow(tr("KDE Bandwidth"), kdeBandwidthSpin_);
    layout->addRow(tr("KDE Grid Resolution"), kdeGridResolutionSpin_);

    // Load current values
    if (monitoringTab_) {
        kdeBandwidthSpin_->setValue(monitoringTab_->getKdeBandwidth());
        kdeGridResolutionSpin_->setValue(monitoringTab_->getKdeGridResolution());
    } else {
        kdeBandwidthSpin_->setValue(50.0);
        kdeGridResolutionSpin_->setValue(50);
    }

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    layout->addRow(buttons_);

    connect(buttons_, &QDialogButtonBox::accepted, this, &MonitoringSettingsDialog::onOk);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons_->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &MonitoringSettingsDialog::onApply);
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
        monitoringTab_->setKdeBandwidth(kdeBandwidthSpin_->value());
        monitoringTab_->setKdeGridResolution(kdeGridResolutionSpin_->value());
        SPDLOG_INFO("Monitoring settings applied: KDE bandwidth={}, grid resolution={}",
                    kdeBandwidthSpin_->value(), kdeGridResolutionSpin_->value());
    }
}


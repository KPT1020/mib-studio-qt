#include "frontend/ProcessingSettingsDialog.h"

#include <QFormLayout>
#include <QSpinBox>
#include <QDialogButtonBox>
#include <QPushButton>

#include <spdlog/spdlog.h>

#include "backend/AppBackend.h"
#include "backend/services/ProcessingService.h"

ProcessingSettingsDialog::ProcessingSettingsDialog(backend::AppBackend& backend, QWidget* parent)
    : QDialog(parent), backend_(backend) {
    setWindowTitle(tr("Processing Settings"));
    setModal(true);

    auto* layout = new QFormLayout(this);

    invalidSamplingSpin_ = new QSpinBox(this);
    invalidSamplingSpin_->setMinimum(1);
    invalidSamplingSpin_->setMaximum(10000);
    invalidSamplingSpin_->setSuffix(tr("th frame"));
    invalidSamplingSpin_->setToolTip(tr("Save every Nth invalid frame (1 = save all, higher = fewer frames)"));

    flushIntervalSpin_ = new QSpinBox(this);
    flushIntervalSpin_->setMinimum(1);
    flushIntervalSpin_->setMaximum(10000);
    flushIntervalSpin_->setSuffix(tr(" frames"));
    flushIntervalSpin_->setToolTip(tr("Flush buffered frames to HDF5 every N frames"));

    layout->addRow(tr("Invalid frame sampling"), invalidSamplingSpin_);
    layout->addRow(tr("Flush interval"), flushIntervalSpin_);

    // Load current values from backend
    invalidSamplingSpin_->setValue(static_cast<int>(backend_.processing().getInvalidFrameSamplingRate()));
    flushIntervalSpin_->setValue(static_cast<int>(backend_.processing().getFlushInterval()));

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    layout->addRow(buttons_);

    connect(buttons_, &QDialogButtonBox::accepted, this, [this]() {
        applySettings();
        accept();
    });
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons_->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this](bool) { onApply(); });
}

void ProcessingSettingsDialog::onApply() {
    applySettings();
}

void ProcessingSettingsDialog::applySettings() {
    auto& proc = backend_.processing();
    const int invalidNth = invalidSamplingSpin_->value();
    const int flushEvery = flushIntervalSpin_->value();

    proc.setInvalidFrameSamplingRate(static_cast<size_t>(invalidNth));
    proc.setFlushInterval(static_cast<size_t>(flushEvery));
    SPDLOG_INFO("Processing settings applied: invalidNth={}, flushEvery={}", invalidNth, flushEvery);
}



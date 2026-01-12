#include "frontend/ConversionFactorDialog.h"

#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLabel>

#include <spdlog/spdlog.h>

#include "backend/AppBackend.h"
#include "backend/services/ProcessingService.h"

ConversionFactorDialog::ConversionFactorDialog(backend::AppBackend& backend, QWidget* parent)
    : QDialog(parent), backend_(backend) {
    setWindowTitle(tr("Pixel to Micron Conversion"));
    setModal(true);

    auto* layout = new QFormLayout(this);

    conversionFactorSpin_ = new QDoubleSpinBox(this);
    conversionFactorSpin_->setMinimum(0.0001);
    conversionFactorSpin_->setMaximum(1000.0);
    conversionFactorSpin_->setDecimals(4);
    conversionFactorSpin_->setSuffix(tr(" μm/pixel"));
    conversionFactorSpin_->setToolTip(tr("Conversion factor: 1 pixel = X micron"));

    layout->addRow(tr("Pixel to Micron Factor"), conversionFactorSpin_);

    // Load current value from backend
    conversionFactorSpin_->setValue(backend_.processing().getPixelToMicronFactor());

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    layout->addRow(buttons_);

    connect(buttons_, &QDialogButtonBox::accepted, this, [this]() {
        applySettings();
        accept();
    });
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons_->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this](bool) { onApply(); });
}

void ConversionFactorDialog::onApply() {
    applySettings();
}

void ConversionFactorDialog::applySettings() {
    auto& proc = backend_.processing();
    const double factor = conversionFactorSpin_->value();

    proc.setPixelToMicronFactor(factor);
    SPDLOG_INFO("Pixel to micron conversion factor applied: {}", factor);
}

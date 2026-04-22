#include "frontend/dialogs/SyringePumpSettingsDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

#include "backend/Tools.h"

namespace {
    constexpr uint16_t VOL_UNIT_UL = 100;
    constexpr uint16_t VOL_UNIT_ML = 103;
}

SyringePumpSettingsDialog::SyringePumpSettingsDialog(
    backend::services::SyringePumpService& service,
    std::function<QStringList()> externallyReservedPortsFn,
    QWidget* parent)
    : QDialog(parent),
      service_(service),
      externallyReservedPortsFn_(std::move(externallyReservedPortsFn))
{
    setWindowTitle(tr("Syringe Pump Settings"));
    resize(640, 480);
    buildUi();

    // Populate rows from the current service state.
    for (auto id : service_.pumpHandles()) {
        appendRowForExistingPump(id);
    }
    refreshAllPortCombos();
}

SyringePumpSettingsDialog::~SyringePumpSettingsDialog() = default;

void SyringePumpSettingsDialog::buildUi() {
    auto* outer = new QVBoxLayout(this);

    auto* top = new QHBoxLayout();
    addBtn_ = new QPushButton(tr("+ Add Pump"), this);
    refreshBtn_ = new QPushButton(tr("Refresh Ports"), this);
    top->addWidget(addBtn_);
    top->addWidget(refreshBtn_);
    top->addStretch(1);
    outer->addLayout(top);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto* inner = new QWidget(scroll);
    rowsLayout_ = new QVBoxLayout(inner);
    rowsLayout_->setContentsMargins(0, 0, 0, 0);
    rowsLayout_->addStretch(1);
    scroll->setWidget(inner);
    outer->addWidget(scroll, 1);

    auto* bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply,
        this);
    outer->addWidget(bb);

    connect(addBtn_,     &QPushButton::clicked, this, &SyringePumpSettingsDialog::onAddPump);
    connect(refreshBtn_, &QPushButton::clicked, this, &SyringePumpSettingsDialog::onRefresh);
    connect(bb,          &QDialogButtonBox::accepted, this, &SyringePumpSettingsDialog::onAccept);
    connect(bb,          &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, &SyringePumpSettingsDialog::onApply);
}

void SyringePumpSettingsDialog::appendRowForExistingPump(PumpHandle id) {
    Row r;
    r.handle = id;
    r.frame = new QGroupBox(QString::fromStdString(service_.pumpName(id)), this);
    auto* form = new QFormLayout(r.frame);

    r.nameEdit = new QLineEdit(QString::fromStdString(service_.pumpName(id)), r.frame);
    form->addRow(tr("Name:"), r.nameEdit);

    r.portCombo = new QComboBox(r.frame);
    form->addRow(tr("Serial port:"), r.portCombo);

    r.baudSpin = new QSpinBox(r.frame);
    r.baudSpin->setRange(1200, 921600);
    r.baudSpin->setSingleStep(1200);
    r.baudSpin->setValue(115200);
    form->addRow(tr("Baud rate:"), r.baudSpin);

    r.addressSpin = new QSpinBox(r.frame);
    r.addressSpin->setRange(1, 247);
    r.addressSpin->setValue(1);
    form->addRow(tr("Modbus address:"), r.addressSpin);

    r.syringeVolSpin = new QDoubleSpinBox(r.frame);
    r.syringeVolSpin->setRange(1.0, 9999.0);
    r.syringeVolSpin->setDecimals(0);
    r.syringeVolSpin->setValue(10);
    form->addRow(tr("Syringe volume:"), r.syringeVolSpin);

    r.syringeUnitCombo = new QComboBox(r.frame);
    r.syringeUnitCombo->addItem(tr("µL"), VOL_UNIT_UL);
    r.syringeUnitCombo->addItem(tr("mL"), VOL_UNIT_ML);
    form->addRow(tr("Syringe volume unit:"), r.syringeUnitCombo);

    // Pre-populate from the service config.
    const auto cfg = service_.getConfig(id);
    r.baudSpin->setValue(cfg.baudRate);
    r.addressSpin->setValue(cfg.modbusAddress);
    r.syringeVolSpin->setValue(cfg.syringeVolume);
    const int uIdx = r.syringeUnitCombo->findData(cfg.syringeVolumeUnit);
    if (uIdx >= 0) r.syringeUnitCombo->setCurrentIndex(uIdx);

    r.removeBtn = new QPushButton(tr("Remove this pump"), r.frame);
    form->addRow(r.removeBtn);

    // Name changes update the group box title live.
    connect(r.nameEdit, &QLineEdit::textChanged, r.frame,
            [frame = r.frame](const QString& s) { if (auto* gb = qobject_cast<QGroupBox*>(frame)) gb->setTitle(s); });

    // When the user changes this row's port, other rows need to refresh to
    // exclude it.
    connect(r.portCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { refreshAllPortCombos(); });

    connect(r.removeBtn, &QPushButton::clicked, this,
            [this, id]() { removeRow(id); });

    const int insertAt = rowsLayout_->count() - 1;  // before trailing stretch
    rowsLayout_->insertWidget(insertAt, r.frame);
    rows_.push_back(r);

    // Populate the new row's port combo and restore prior selection if any.
    refreshAllPortCombos();
    if (!cfg.portName.isEmpty()) {
        const int i = r.portCombo->findData(cfg.portName);
        if (i >= 0) r.portCombo->setCurrentIndex(i);
    }
}

void SyringePumpSettingsDialog::appendRowForNewPump() {
    const std::string name = "Pump " + std::to_string(service_.pumpCount() + 1);
    const auto id = service_.addPump(name);
    appendRowForExistingPump(id);

    // Assign a default COM port not already in use by another row.
    const auto exclude = reservedFor(id);
    const QString picked = firstFreePort(exclude);
    if (!picked.isEmpty()) {
        auto& r = rows_.back();
        const int i = r.portCombo->findData(picked);
        if (i >= 0) r.portCombo->setCurrentIndex(i);
    }
    emit pumpsChanged();
}

void SyringePumpSettingsDialog::removeRow(PumpHandle id) {
    for (auto it = rows_.begin(); it != rows_.end(); ++it) {
        if (it->handle == id) {
            it->frame->deleteLater();
            rows_.erase(it);
            break;
        }
    }
    service_.removePump(id);
    refreshAllPortCombos();
    emit pumpsChanged();
}

QStringList SyringePumpSettingsDialog::currentlyChosenPorts(PumpHandle exclude) const {
    QStringList out;
    for (const auto& r : rows_) {
        if (r.handle == exclude) continue;
        const QString p = r.portCombo->currentData().toString();
        if (!p.isEmpty()) out.append(p);
    }
    return out;
}

QStringList SyringePumpSettingsDialog::reservedFor(PumpHandle row) const {
    QStringList out = currentlyChosenPorts(row);
    if (externallyReservedPortsFn_) out += externallyReservedPortsFn_();
    out.removeDuplicates();
    return out;
}

QString SyringePumpSettingsDialog::firstFreePort(const QStringList& exclude) const {
    const auto all = backend::Tools::availableSerialPortNames();
    for (const QString& n : all) {
        if (!exclude.contains(n)) return n;
    }
    return {};
}

void SyringePumpSettingsDialog::refreshAllPortCombos() {
    const QStringList all = backend::Tools::availableSerialPortNames();

    for (auto& r : rows_) {
        const QString prev = r.portCombo->currentData().toString();
        const QStringList exclude = reservedFor(r.handle);

        QSignalBlocker block(r.portCombo);
        r.portCombo->clear();
        for (const QString& name : all) {
            if (exclude.contains(name) && name != prev) continue;
            r.portCombo->addItem(name, name);
        }
        // Ensure the current selection (if valid) is present and selected.
        if (!prev.isEmpty()) {
            const int i = r.portCombo->findData(prev);
            if (i < 0) {
                r.portCombo->addItem(prev + tr(" (not detected)"), prev);
                r.portCombo->setCurrentIndex(r.portCombo->count() - 1);
            } else {
                r.portCombo->setCurrentIndex(i);
            }
        }
    }
}

void SyringePumpSettingsDialog::onAddPump() {
    appendRowForNewPump();
}

void SyringePumpSettingsDialog::onRefresh() {
    refreshAllPortCombos();
}

void SyringePumpSettingsDialog::onApply() {
    // Push UI values into the service config; live-apply syringe volume for
    // already-connected pumps.
    for (const auto& r : rows_) {
        service_.setPumpName(r.handle, r.nameEdit->text().trimmed().toStdString());

        auto pc = service_.getConfig(r.handle);
        pc.portName = r.portCombo->currentData().toString();
        pc.baudRate = r.baudSpin->value();
        pc.modbusAddress = static_cast<uint8_t>(r.addressSpin->value());
        pc.syringeVolume = static_cast<uint16_t>(r.syringeVolSpin->value());
        pc.syringeVolumeUnit = static_cast<uint16_t>(r.syringeUnitCombo->currentData().toUInt());
        service_.setConfig(r.handle, pc);

        if (service_.isConnected(r.handle)) {
            service_.setSyringeVolume(r.handle, pc.syringeVolume, pc.syringeVolumeUnit);
        }
    }
    emit pumpsChanged();
}

void SyringePumpSettingsDialog::onAccept() {
    onApply();
    accept();
}

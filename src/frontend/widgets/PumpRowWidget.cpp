#include "frontend/widgets/PumpRowWidget.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

using Direction = backend::services::SyringePumpService::Direction;
using RunStatus = backend::services::SyringePumpService::RunStatus;

namespace frontend {

namespace {
    QString runStatusToString(RunStatus s) {
        switch (s) {
            case RunStatus::Stop:     return QObject::tr("Stopped");
            case RunStatus::Forward:  return QObject::tr("Running (Forward)");
            case RunStatus::Backward: return QObject::tr("Running (Backward)");
            case RunStatus::Pause:    return QObject::tr("Paused");
        }
        return QObject::tr("Unknown");
    }

    constexpr uint16_t UNIT_UL_MIN = 100;
    constexpr uint16_t UNIT_ML_MIN = 103;
}

PumpRowWidget::PumpRowWidget(backend::services::SyringePumpService& svc,
                             PumpHandle handle,
                             QWidget* parent)
    : QGroupBox(parent), service_(svc), handle_(handle)
{
    buildUi();
    wireSignals();
    refresh();
}

QString PumpRowWidget::pumpName() const {
    return nameEdit_ ? nameEdit_->text() : QString();
}

void PumpRowWidget::setPumpName(const QString& name) {
    if (!nameEdit_) return;
    QSignalBlocker b(nameEdit_);
    nameEdit_->setText(name);
    setTitle(name);
    service_.setPumpName(handle_, name.toStdString());
}

double PumpRowWidget::flowRate() const {
    return flowRateSpin_ ? flowRateSpin_->value() : 0.0;
}

uint16_t PumpRowWidget::flowUnit() const {
    if (!flowUnitCombo_) return UNIT_UL_MIN;
    return flowUnitCombo_->currentIndex() == 0 ? UNIT_UL_MIN : UNIT_ML_MIN;
}

int PumpRowWidget::directionIndex() const {
    return directionCombo_ ? directionCombo_->currentIndex() : 0;
}

void PumpRowWidget::setLiveSettings(double rate, uint16_t unit, int directionIndex) {
    if (flowRateSpin_)   { QSignalBlocker b(flowRateSpin_);   flowRateSpin_->setValue(rate); }
    if (flowUnitCombo_)  { QSignalBlocker b(flowUnitCombo_);  flowUnitCombo_->setCurrentIndex(unit == UNIT_ML_MIN ? 1 : 0); }
    if (directionCombo_) { QSignalBlocker b(directionCombo_); directionCombo_->setCurrentIndex(directionIndex == 1 ? 1 : 0); }
}

void PumpRowWidget::buildUi() {
    const auto name = QString::fromStdString(service_.pumpName(handle_));
    setTitle(name);
    setCheckable(false);

    auto* outer = new QVBoxLayout(this);

    // --- Header row: editable name + Remove button ------------------------
    auto* header = new QHBoxLayout();
    nameEdit_ = new QLineEdit(name, this);
    nameEdit_->setPlaceholderText(tr("Pump name"));
    header->addWidget(new QLabel(tr("Name:"), this));
    header->addWidget(nameEdit_, 1);

    portLabel_ = new QLabel(tr("(not connected)"), this);
    portLabel_->setMinimumWidth(140);
    header->addWidget(portLabel_);

    removeBtn_ = new QPushButton(tr("Remove"), this);
    header->addWidget(removeBtn_);
    outer->addLayout(header);

    // --- Grid of controls -------------------------------------------------
    auto* grid = new QGridLayout();
    int row = 0;

    connectBtn_    = new QPushButton(tr("Connect"), this);
    disconnectBtn_ = new QPushButton(tr("Disconnect"), this);
    grid->addWidget(connectBtn_,    row, 0);
    grid->addWidget(disconnectBtn_, row, 1);

    statusLabel_ = new QLabel(tr("Not connected"), this);
    grid->addWidget(new QLabel(tr("Status:"), this), row, 2);
    grid->addWidget(statusLabel_, row, 3, 1, 2);
    ++row;

    flowRateSpin_ = new QDoubleSpinBox(this);
    flowRateSpin_->setRange(0.0, 9999.0);
    flowRateSpin_->setDecimals(2);
    flowRateSpin_->setValue(1.0);

    flowUnitCombo_ = new QComboBox(this);
    flowUnitCombo_->addItem(tr("µL/min"));
    flowUnitCombo_->addItem(tr("mL/min"));

    directionCombo_ = new QComboBox(this);
    directionCombo_->addItem(tr("Infuse"));
    directionCombo_->addItem(tr("Withdraw"));

    grid->addWidget(new QLabel(tr("Flow:"), this), row, 0);
    grid->addWidget(flowRateSpin_,   row, 1);
    grid->addWidget(flowUnitCombo_,  row, 2);
    grid->addWidget(new QLabel(tr("Direction:"), this), row, 3);
    grid->addWidget(directionCombo_, row, 4);
    ++row;

    startBtn_ = new QPushButton(tr("Start"), this);
    stopBtn_  = new QPushButton(tr("Stop"),  this);
    purgeBtn_ = new QPushButton(tr("Purge (hold)"), this);
    grid->addWidget(startBtn_, row, 0);
    grid->addWidget(stopBtn_,  row, 1);
    grid->addWidget(purgeBtn_, row, 2);

    flowLabel_   = new QLabel(QStringLiteral("--"), this);
    volumeLabel_ = new QLabel(QStringLiteral("--"), this);
    grid->addWidget(new QLabel(tr("Current flow:"), this), row, 3);
    grid->addWidget(flowLabel_, row, 4);
    ++row;
    grid->addWidget(new QLabel(tr("Volume:"), this), row, 3);
    grid->addWidget(volumeLabel_, row, 4);

    outer->addLayout(grid);

    applyTimer_ = new QTimer(this);
    applyTimer_->setSingleShot(true);
    applyTimer_->setInterval(300);
}

void PumpRowWidget::wireSignals() {
    connect(connectBtn_,    &QPushButton::clicked, this, &PumpRowWidget::onConnect);
    connect(disconnectBtn_, &QPushButton::clicked, this, &PumpRowWidget::onDisconnect);
    connect(startBtn_,      &QPushButton::clicked, this, &PumpRowWidget::onStart);
    connect(stopBtn_,       &QPushButton::clicked, this, &PumpRowWidget::onStop);
    connect(purgeBtn_,      &QPushButton::pressed,  this, &PumpRowWidget::onPurgePressed);
    connect(purgeBtn_,      &QPushButton::released, this, &PumpRowWidget::onPurgeReleased);
    connect(removeBtn_,     &QPushButton::clicked, this, &PumpRowWidget::onRemoveClicked);

    connect(nameEdit_, &QLineEdit::editingFinished, this, &PumpRowWidget::onNameEdited);

    connect(flowRateSpin_,  &QDoubleSpinBox::editingFinished, this, [this]() { scheduleApply(); });
    connect(flowUnitCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { scheduleApply(); });
    connect(directionCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { scheduleApply(); });
    connect(applyTimer_, &QTimer::timeout, this, &PumpRowWidget::onApply);
}

void PumpRowWidget::scheduleApply() {
    // Only push to hardware if already connected; always emit change signal so
    // the parent tab can persist the value to config.
    emit liveSettingsChanged(handle_);
    if (service_.isConnected(handle_)) applyTimer_->start();
}

void PumpRowWidget::onConnect() {
    auto cfg = service_.getConfig(handle_);
    if (cfg.portName.isEmpty()) {
        statusLabel_->setText(tr("No port configured — open Pump Settings"));
        return;
    }
    const bool ok = service_.connect(handle_, cfg.portName, cfg.baudRate, cfg.modbusAddress);
    if (!ok) {
        statusLabel_->setText(tr("Failed to connect on %1").arg(cfg.portName));
    } else {
        // Push current syringe volume (cached in config) to the pump.
        if (cfg.syringeVolume > 0) {
            service_.setSyringeVolume(handle_, cfg.syringeVolume, cfg.syringeVolumeUnit);
        }
    }
    refresh();
}

void PumpRowWidget::onDisconnect() {
    service_.disconnect(handle_);
    refresh();
}

void PumpRowWidget::onStart() { service_.start(handle_); }
void PumpRowWidget::onStop()  { service_.stop(handle_);  }

void PumpRowWidget::onPurgePressed() {
    const auto dir = directionIndex() == 0 ? Direction::Infuse : Direction::Withdraw;
    service_.purge(handle_, dir);
}

void PumpRowWidget::onPurgeReleased() {
    service_.stopPurge(handle_);
}

void PumpRowWidget::onApply() {
    service_.setFlowRate(handle_, flowRate(), flowUnit());
    service_.setDirection(handle_,
        directionIndex() == 0 ? Direction::Infuse : Direction::Withdraw);
}

void PumpRowWidget::onNameEdited() {
    const QString newName = nameEdit_->text().trimmed();
    if (newName.isEmpty()) {
        nameEdit_->setText(QString::fromStdString(service_.pumpName(handle_)));
        return;
    }
    service_.setPumpName(handle_, newName.toStdString());
    setTitle(newName);
    emit nameChanged(handle_, newName);
}

void PumpRowWidget::onRemoveClicked() {
    emit removeRequested(handle_);
}

void PumpRowWidget::refresh() {
    if (service_.pumpCount() == 0) return;

    // Poll hardware first; getStatus reflects the poll result.
    if (service_.isConnected(handle_)) {
        service_.pollStatus(handle_);
    }

    const auto status = service_.getStatus(handle_);
    const bool connected = status.connected;

    connectBtn_->setEnabled(!connected);
    disconnectBtn_->setEnabled(connected);
    flowRateSpin_->setEnabled(connected);
    flowUnitCombo_->setEnabled(connected);
    directionCombo_->setEnabled(connected);
    startBtn_->setEnabled(connected);
    stopBtn_->setEnabled(connected);
    purgeBtn_->setEnabled(connected);

    if (connected) {
        QString statusText = runStatusToString(status.runStatus);
        if (status.stalled) statusText += tr(" [STALL]");
        statusLabel_->setText(statusText);
        flowLabel_->setText(QString::number(status.currentFlowRate, 'f', 4));
        volumeLabel_->setText(QString::number(status.accumulatedVolume, 'f', 4));
        const QString pname = service_.getPortName(handle_);
        portLabel_->setText(pname.isEmpty() ? tr("(unknown port)") : pname);
    } else {
        statusLabel_->setText(tr("Not connected"));
        flowLabel_->setText(QStringLiteral("--"));
        volumeLabel_->setText(QStringLiteral("--"));
        const auto cfg = service_.getConfig(handle_);
        portLabel_->setText(cfg.portName.isEmpty()
            ? tr("(no port configured)") : cfg.portName);
    }
}

} // namespace frontend

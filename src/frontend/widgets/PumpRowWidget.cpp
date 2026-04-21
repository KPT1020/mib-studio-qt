#include "frontend/widgets/PumpRowWidget.h"

#include "ui_PumpRowWidget.h"

#include <QSignalBlocker>
#include <QTimer>

namespace frontend {

namespace {
QString runStatusToString(backend::services::SyringePumpService::RunStatus status) {
    using RunStatus = backend::services::SyringePumpService::RunStatus;
    switch (status) {
        case RunStatus::Stop:
            return QStringLiteral("Stopped");
        case RunStatus::Forward:
            return QStringLiteral("Running (Forward)");
        case RunStatus::Backward:
            return QStringLiteral("Running (Backward)");
        case RunStatus::Pause:
            return QStringLiteral("Paused");
        default:
            return QStringLiteral("Unknown");
    }
}

constexpr int kBaudRates[] = {9600, 19200, 38400, 57600, 115200};
constexpr int kBaudRateCount = static_cast<int>(sizeof(kBaudRates) / sizeof(kBaudRates[0]));
} // namespace

PumpRowWidget::PumpRowWidget(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::PumpRowWidget)
    , applyTimer_(new QTimer(this)) {
    ui->setupUi(this);

    ui->syringeUnitCombo->setItemData(0, 100);
    ui->syringeUnitCombo->setItemData(1, 103);

    applyTimer_->setSingleShot(true);
    applyTimer_->setInterval(300);
    connect(applyTimer_, &QTimer::timeout, this, &PumpRowWidget::applyRequested);

    connect(ui->connectBtn, &QPushButton::clicked, this, &PumpRowWidget::connectRequested);
    connect(ui->disconnectBtn, &QPushButton::clicked, this, &PumpRowWidget::disconnectRequested);
    connect(ui->startBtn, &QPushButton::clicked, this, &PumpRowWidget::startRequested);
    connect(ui->stopBtn, &QPushButton::clicked, this, &PumpRowWidget::stopRequested);
    connect(ui->removeBtn, &QPushButton::clicked, this, &PumpRowWidget::removeRequested);
    connect(ui->portRefreshBtn, &QPushButton::clicked, this, &PumpRowWidget::portRefreshRequested);

    connect(ui->purgeBtn, &QPushButton::pressed, this, [this]() {
        emit purgeRequested(viewState().direction);
    });
    connect(ui->purgeBtn, &QPushButton::released, this, &PumpRowWidget::stopPurgeRequested);

    connect(ui->nameEdit, &QLineEdit::editingFinished, this, [this]() {
        emit nameChanged(pumpName());
    });
    connect(ui->flowRateSpinBox, &QDoubleSpinBox::editingFinished, this, &PumpRowWidget::scheduleApply);
    connect(ui->flowUnitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        scheduleApply();
    });
    connect(ui->directionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        scheduleApply();
    });

    connect(ui->portCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        emit settingsChanged();
    });
    connect(ui->baudCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        emit settingsChanged();
    });
    connect(ui->addressSpinBox, &QSpinBox::editingFinished, this, [this]() {
        emit settingsChanged();
    });
    connect(ui->syringeVolSpinBox, &QSpinBox::editingFinished, this, [this]() {
        emit settingsChanged();
    });
    connect(ui->syringeUnitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        emit settingsChanged();
    });
}

PumpRowWidget::~PumpRowWidget() {
    delete ui;
}

void PumpRowWidget::setPumpName(const QString& name) {
    QSignalBlocker blocker(ui->nameEdit);
    ui->nameEdit->setText(name);
}

QString PumpRowWidget::pumpName() const {
    return ui->nameEdit->text().trimmed();
}

void PumpRowWidget::setViewState(const ViewState& state) {
    QSignalBlocker b1(ui->flowRateSpinBox);
    QSignalBlocker b2(ui->flowUnitCombo);
    QSignalBlocker b3(ui->directionCombo);
    QSignalBlocker b4(ui->baudCombo);
    QSignalBlocker b5(ui->addressSpinBox);
    QSignalBlocker b6(ui->syringeVolSpinBox);
    QSignalBlocker b7(ui->syringeUnitCombo);

    ui->flowRateSpinBox->setValue(state.flowRate);
    ui->flowUnitCombo->setCurrentIndex(comboIndexFromFlowRateUnit(state.flowRateUnit));
    ui->directionCombo->setCurrentIndex(state.direction ==
                                                backend::services::SyringePumpService::Direction::Infuse
                                            ? 0
                                            : 1);
    ui->baudCombo->setCurrentIndex(comboIndexFromBaudRate(state.baudRate));
    ui->addressSpinBox->setValue(static_cast<int>(state.modbusAddress));
    ui->syringeVolSpinBox->setValue(static_cast<int>(state.syringeVolume));
    ui->syringeUnitCombo->setCurrentIndex(comboIndexFromSyringeUnit(state.syringeVolumeUnit));
}

PumpRowWidget::ViewState PumpRowWidget::viewState() const {
    ViewState state;
    state.portName = ui->portCombo->currentData().toString();
    state.baudRate = baudRateFromComboIndex(ui->baudCombo->currentIndex());
    state.modbusAddress = static_cast<uint8_t>(ui->addressSpinBox->value());
    state.flowRate = ui->flowRateSpinBox->value();
    state.flowRateUnit = flowRateUnitFromComboIndex(ui->flowUnitCombo->currentIndex());
    state.direction = ui->directionCombo->currentIndex() == 0
                          ? backend::services::SyringePumpService::Direction::Infuse
                          : backend::services::SyringePumpService::Direction::Withdraw;
    state.syringeVolume = static_cast<uint16_t>(ui->syringeVolSpinBox->value());
    state.syringeVolumeUnit = syringeUnitFromComboIndex(ui->syringeUnitCombo->currentIndex());
    return state;
}

void PumpRowWidget::setPortChoices(const QStringList& availablePorts, const QString& currentSelection) {
    QSignalBlocker blocker(ui->portCombo);
    ui->portCombo->clear();
    ui->portCombo->addItem(tr("(Not set)"), QString{});

    bool currentSelectionPresent = false;
    for (const auto& port : availablePorts) {
        ui->portCombo->addItem(port, port);
        if (!currentSelection.isEmpty() && port.compare(currentSelection, Qt::CaseInsensitive) == 0) {
            currentSelectionPresent = true;
        }
    }
    if (!currentSelection.isEmpty() && !currentSelectionPresent) {
        ui->portCombo->addItem(currentSelection, currentSelection);
    }

    int idx = ui->portCombo->findData(currentSelection);
    if (idx < 0) {
        idx = 0;
    }
    ui->portCombo->setCurrentIndex(idx);
}

void PumpRowWidget::setConnected(bool connected) {
    connected_ = connected;
    ui->connectBtn->setEnabled(!connected);
    ui->disconnectBtn->setEnabled(connected);
    ui->portCombo->setEnabled(!connected);
    ui->portRefreshBtn->setEnabled(!connected);
    ui->baudCombo->setEnabled(!connected);
    ui->addressSpinBox->setEnabled(!connected);
    ui->flowRateSpinBox->setEnabled(connected);
    ui->flowUnitCombo->setEnabled(connected);
    ui->directionCombo->setEnabled(connected);
    ui->startBtn->setEnabled(connected);
    ui->stopBtn->setEnabled(connected);
    ui->purgeBtn->setEnabled(connected);

    if (!connected) {
        ui->statusLabel->setText(QStringLiteral("Not connected"));
        ui->flowStatusLabel->setText(QStringLiteral("--"));
        ui->volumeLabel->setText(QStringLiteral("--"));
    }
}

void PumpRowWidget::setStatus(const backend::services::SyringePumpService::PumpStatus& status) {
    QString statusText = runStatusToString(status.runStatus);
    if (status.stalled) {
        statusText += QStringLiteral(" [STALL]");
    }
    ui->statusLabel->setText(statusText);
    ui->flowStatusLabel->setText(QString::number(status.currentFlowRate, 'f', 4));
    ui->volumeLabel->setText(QString::number(status.accumulatedVolume, 'f', 4));
}

void PumpRowWidget::setDisconnectedStatus() {
    ui->statusLabel->setText(QStringLiteral("Not connected"));
    ui->flowStatusLabel->setText(QStringLiteral("--"));
    ui->volumeLabel->setText(QStringLiteral("--"));
}

void PumpRowWidget::setRemoveEnabled(bool enabled) {
    ui->removeBtn->setEnabled(enabled);
}

uint16_t PumpRowWidget::flowRateUnitFromComboIndex(int index) {
    return index == 0 ? 100 : 103;
}

int PumpRowWidget::comboIndexFromFlowRateUnit(uint16_t unit) {
    return unit == 100 ? 0 : 1;
}

uint16_t PumpRowWidget::syringeUnitFromComboIndex(int index) {
    return index == 0 ? 100 : 103;
}

int PumpRowWidget::comboIndexFromSyringeUnit(uint16_t unit) {
    return unit == 100 ? 0 : 1;
}

int PumpRowWidget::comboIndexFromBaudRate(int baud) {
    for (int i = 0; i < kBaudRateCount; ++i) {
        if (kBaudRates[i] == baud) {
            return i;
        }
    }
    return kBaudRateCount - 1;
}

int PumpRowWidget::baudRateFromComboIndex(int index) {
    if (index < 0 || index >= kBaudRateCount) {
        return 115200;
    }
    return kBaudRates[index];
}

void PumpRowWidget::scheduleApply() {
    if (!connected_) {
        return;
    }
    applyTimer_->start();
}

} // namespace frontend

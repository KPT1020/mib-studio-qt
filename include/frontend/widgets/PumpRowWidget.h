#pragma once

#include "backend/services/SyringePumpService.h"

#include <QStringList>
#include <QWidget>

class QTimer;

namespace Ui {
class PumpRowWidget;
}

namespace frontend {

class PumpRowWidget : public QWidget {
    Q_OBJECT
public:
    struct ViewState {
        QString portName{};
        int baudRate{115200};
        uint8_t modbusAddress{1};
        double flowRate{0.0};
        uint16_t flowRateUnit{100};
        backend::services::SyringePumpService::Direction direction{
            backend::services::SyringePumpService::Direction::Infuse};
        uint16_t syringeVolume{100};
        uint16_t syringeVolumeUnit{100};
    };

    explicit PumpRowWidget(QWidget* parent = nullptr);
    ~PumpRowWidget();

    void setPumpName(const QString& name);
    QString pumpName() const;

    void setViewState(const ViewState& state);
    ViewState viewState() const;
    bool isAddressEditInProgress() const;

    void setPortChoices(const QStringList& availablePorts, const QString& currentSelection);

    void setConnected(bool connected);
    void setStatus(const backend::services::SyringePumpService::PumpStatus& status);
    void setDisconnectedStatus();
    void setRemoveEnabled(bool enabled);

signals:
    void connectRequested();
    void disconnectRequested();
    void startRequested();
    void stopRequested();
    void purgeRequested(backend::services::SyringePumpService::Direction direction);
    void stopPurgeRequested();
    void applyRequested();
    void removeRequested();
    void nameChanged(const QString& name);
    void portRefreshRequested();
    void settingsChanged();

private:
    static uint16_t flowRateUnitFromComboIndex(int index);
    static int comboIndexFromFlowRateUnit(uint16_t unit);
    static uint16_t syringeUnitFromComboIndex(int index);
    static int comboIndexFromSyringeUnit(uint16_t unit);
    static int comboIndexFromBaudRate(int baud);
    static int baudRateFromComboIndex(int index);

    void scheduleApply();

    Ui::PumpRowWidget* ui{nullptr};
    QTimer* applyTimer_{nullptr};
    bool connected_{false};
};

} // namespace frontend

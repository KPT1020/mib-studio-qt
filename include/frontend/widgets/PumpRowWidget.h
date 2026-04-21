#pragma once

#include "backend/services/SyringePumpService.h"

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
        double flowRate{0.0};
        uint16_t flowRateUnit{100};
        backend::services::SyringePumpService::Direction direction{
            backend::services::SyringePumpService::Direction::Infuse};
    };

    explicit PumpRowWidget(QWidget* parent = nullptr);
    ~PumpRowWidget();

    void setPumpName(const QString& name);
    QString pumpName() const;

    void setViewState(const ViewState& state);
    ViewState viewState() const;

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

private:
    static uint16_t flowRateUnitFromComboIndex(int index);
    static int comboIndexFromFlowRateUnit(uint16_t unit);
    void scheduleApply();

    Ui::PumpRowWidget* ui{nullptr};
    QTimer* applyTimer_{nullptr};
    bool connected_{false};
};

} // namespace frontend

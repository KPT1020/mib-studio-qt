#pragma once

#include <QGroupBox>

#include "backend/services/SyringePumpService.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;

namespace frontend {

// Self-contained row of controls for a single pump. Owns no state beyond the
// handle — all pump state lives in SyringePumpService. Parent is responsible
// for calling refresh() periodically (e.g. every 500 ms) and for responding
// to removeRequested().
class PumpRowWidget : public QGroupBox {
    Q_OBJECT
public:
    using PumpHandle = backend::services::SyringePumpService::PumpHandle;

    PumpRowWidget(backend::services::SyringePumpService& svc,
                  PumpHandle handle,
                  QWidget* parent = nullptr);

    PumpHandle handle() const { return handle_; }
    QString pumpName() const;
    void setPumpName(const QString& name);

    // Serialize per-pump "live" settings (flow rate/unit/direction) from the UI.
    // Used by the parent tab's saveConfig() — the tab writes one JSON object
    // per row.
    double flowRate() const;
    uint16_t flowUnit() const;       // Modbus unit code (100 = µL/min, 103 = mL/min)
    int directionIndex() const;      // 0 = Infuse, 1 = Withdraw
    void setLiveSettings(double rate, uint16_t unit, int directionIndex);

public slots:
    // Re-read pump status and repaint this row's controls/labels.
    void refresh();

signals:
    void removeRequested(backend::services::SyringePumpService::PumpHandle handle);
    void nameChanged(backend::services::SyringePumpService::PumpHandle handle,
                     const QString& name);
    void liveSettingsChanged(backend::services::SyringePumpService::PumpHandle handle);

private slots:
    void onConnect();
    void onDisconnect();
    void onStart();
    void onStop();
    void onPurgePressed();
    void onPurgeReleased();
    void onApply();        // debounced flow-rate/direction apply
    void onNameEdited();
    void onRemoveClicked();

private:
    void buildUi();
    void wireSignals();
    void scheduleApply();  // restart debounce timer

    backend::services::SyringePumpService& service_;
    PumpHandle handle_{backend::services::SyringePumpService::InvalidHandle};

    QLineEdit*      nameEdit_{nullptr};
    QPushButton*    removeBtn_{nullptr};
    QPushButton*    connectBtn_{nullptr};
    QPushButton*    disconnectBtn_{nullptr};
    QPushButton*    startBtn_{nullptr};
    QPushButton*    stopBtn_{nullptr};
    QPushButton*    purgeBtn_{nullptr};
    QDoubleSpinBox* flowRateSpin_{nullptr};
    QComboBox*      flowUnitCombo_{nullptr};
    QComboBox*      directionCombo_{nullptr};
    QLabel*         statusLabel_{nullptr};
    QLabel*         flowLabel_{nullptr};
    QLabel*         volumeLabel_{nullptr};
    QLabel*         portLabel_{nullptr};
    QTimer*         applyTimer_{nullptr};
};

} // namespace frontend

#pragma once

#include <array>

#include <QWidget>

namespace backend { class AppBackend; }
class QTimer;
namespace Ui { class SyringePumpTab; }

namespace frontend {

class SyringePumpTab : public QWidget {
    Q_OBJECT
public:
    explicit SyringePumpTab(backend::AppBackend& backend, QWidget* parent = nullptr);
    ~SyringePumpTab();

private slots:
    void onConnectSample();
    void onDisconnectSample();
    void onConnectSheath();
    void onDisconnectSheath();
    void onStartSample();
    void onStopSample();
    void onStartSheath();
    void onStopSheath();
    void onApplySample();
    void onApplySheath();
    void onUpdateStatus();

private:
    struct PumpRunControlState {
        bool runUntilStall{false};
        bool requestedStart{false};
    };

    void updatePumpUI(int pumpIndex);
    void loadConfig();
    void saveConfig();
    QString configPath() const;

    // Map flow unit combo index to Modbus register value
    uint16_t flowUnitFromCombo(int comboIndex) const;
    int comboIndexFromFlowUnit(uint16_t unit) const;

    Ui::SyringePumpTab* ui;
    backend::AppBackend& backend_;
    QTimer* statusUpdateTimer_{nullptr};
    QTimer* sampleApplyTimer_{nullptr};
    QTimer* sheathApplyTimer_{nullptr};
    std::array<PumpRunControlState, 2> runControlState_{};
};

} // namespace frontend

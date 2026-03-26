#pragma once

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
    void onRefreshSamplePorts();
    void onRefreshSheathPorts();
    void onUpdateStatus();

private:
    void updatePumpUI(int pumpIndex);
    void loadConfig();
    void saveConfig();
    QString configPath() const;
    void populateComPortList(int pumpIndex);
    void populateSyringeOptions();

    // Map flow unit combo index to Modbus register value
    uint16_t flowUnitFromCombo(int comboIndex) const;
    int comboIndexFromFlowUnit(uint16_t unit) const;

    Ui::SyringePumpTab* ui;
    backend::AppBackend& backend_;
    QTimer* statusUpdateTimer_{nullptr};
};

} // namespace frontend

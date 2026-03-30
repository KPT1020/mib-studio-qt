#include "frontend/tabs/SyringePumpTab.h"
#include "ui_SyringePumpTab.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QTextStream>
#include <QTimer>
#include <QSettings>

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#endif

#include "backend/AppBackend.h"
#include "backend/Tools.h"
#include "backend/services/SyringePumpService.h"
#include "backend/services/AutofocusService.h"

using json = nlohmann::json;
using PumpId = backend::services::SyringePumpService::PumpId;
using Direction = backend::services::SyringePumpService::Direction;
using RunStatus = backend::services::SyringePumpService::RunStatus;

namespace frontend {

namespace {
    static QString getUserConfigDir()
    {
        QString appDir = QCoreApplication::applicationDirPath();
        QString appDirLower = appDir.toLower();

#ifdef _WIN32
        if (appDirLower.contains("program files") ||
            appDirLower.contains("program files (x86)"))
        {
            char appDataPath[MAX_PATH];
            if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appDataPath)))
            {
                QString userConfigDir = QDir(QString::fromStdString(std::string(appDataPath) + "\\MIB_Studio_Qt\\include")).absolutePath();
                QDir().mkpath(userConfigDir);
                return userConfigDir;
            }
        }
#endif
        return QDir(appDir).absoluteFilePath("../include");
    }

    // Syringe manufacturers (from dLSP 501X manual Appendix E)
    struct SyringeMfgEntry {
        uint16_t code;
        const char* name;
    };
    static constexpr SyringeMfgEntry SYRINGE_MFGS[] = {
        {0, "Hamilton"},
        {1, "Popper&sons"},
        {2, "SGE"},
        {3, "Shanghai Gauge"},
        {4, "Unimetrics"},
        {5, "Sherwood Monoject"},
        {6, "Ranfac"},
        {7, "Terumo"},
        {8, "Air-Tite"},
        {9, "Becton Dickinson"},
        {10, "Becton Dickinson (G)"},
        {11, "KimHuaLiLiao"},
        {12, "XinMaJiLiao"},
    };

    // Common syringe volumes (specification codes from manual)
    struct SyringeSpecEntry {
        uint16_t code;
        const char* label;
    };
    static constexpr SyringeSpecEntry SYRINGE_SPECS[] = {
        {0, "0.5 uL"},
        {1, "1 uL"},
        {2, "2 uL"},
        {3, "5 uL"},
        {4, "10 uL"},
        {5, "20 uL"},
        {6, "25 uL"},
        {7, "50 uL"},
        {8, "100 uL"},
        {9, "250 uL"},
        {10, "500 uL"},
        {11, "1 mL"},
        {12, "2 mL"},
        {13, "5 mL"},
        {14, "10 mL"},
        {15, "20 mL"},
        {16, "25 mL"},
        {17, "30 mL"},
        {18, "50 mL"},
        {19, "60 mL"},
    };

    QString runStatusToString(RunStatus status) {
        switch (status) {
            case RunStatus::Stop:     return "Stopped";
            case RunStatus::Forward:  return "Running (Forward)";
            case RunStatus::Backward: return "Running (Backward)";
            case RunStatus::Pause:    return "Paused";
            default:                  return "Unknown";
        }
    }
} // namespace

SyringePumpTab::SyringePumpTab(backend::AppBackend& backend, QWidget* parent)
    : QWidget(parent), ui(new Ui::SyringePumpTab), backend_(backend)
{
    ui->setupUi(this);

    // Configure baud rate combos with data values
    auto setupBaudCombo = [](QComboBox* combo) {
        combo->setItemData(0, 9600);
        combo->setItemData(1, 19200);
        combo->setItemData(2, 38400);
        combo->setItemData(3, 57600);
        combo->setItemData(4, 115200);
        combo->setCurrentIndex(4); // Default 115200 (pump factory default)
    };
    setupBaudCombo(ui->sampleBaudCombo);
    setupBaudCombo(ui->sheathBaudCombo);

    // Populate syringe options
    populateSyringeOptions();

    // Connect Sample pump signals
    connect(ui->sampleConnectBtn, &QPushButton::clicked, this, &SyringePumpTab::onConnectSample);
    connect(ui->sampleDisconnectBtn, &QPushButton::clicked, this, &SyringePumpTab::onDisconnectSample);
    connect(ui->sampleRefreshBtn, &QPushButton::clicked, this, &SyringePumpTab::onRefreshSamplePorts);
    connect(ui->sampleStartBtn, &QPushButton::clicked, this, &SyringePumpTab::onStartSample);
    connect(ui->sampleStopBtn, &QPushButton::clicked, this, &SyringePumpTab::onStopSample);
    connect(ui->sampleApplyBtn, &QPushButton::clicked, this, &SyringePumpTab::onApplySample);

    // Connect Sheath pump signals
    connect(ui->sheathConnectBtn, &QPushButton::clicked, this, &SyringePumpTab::onConnectSheath);
    connect(ui->sheathDisconnectBtn, &QPushButton::clicked, this, &SyringePumpTab::onDisconnectSheath);
    connect(ui->sheathRefreshBtn, &QPushButton::clicked, this, &SyringePumpTab::onRefreshSheathPorts);
    connect(ui->sheathStartBtn, &QPushButton::clicked, this, &SyringePumpTab::onStartSheath);
    connect(ui->sheathStopBtn, &QPushButton::clicked, this, &SyringePumpTab::onStopSheath);
    connect(ui->sheathApplyBtn, &QPushButton::clicked, this, &SyringePumpTab::onApplySheath);

    // Load saved config
    loadConfig();

    // Populate COM port lists
    populateComPortList(0);
    populateComPortList(1);

    // Update UI state
    updatePumpUI(0);
    updatePumpUI(1);

    // Status update timer (500ms)
    statusUpdateTimer_ = new QTimer(this);
    statusUpdateTimer_->setInterval(500);
    connect(statusUpdateTimer_, &QTimer::timeout, this, &SyringePumpTab::onUpdateStatus);
    statusUpdateTimer_->start();

    // Save config on app quit
    connect(qApp, &QApplication::aboutToQuit, this, [this]() { saveConfig(); });
}

SyringePumpTab::~SyringePumpTab() {
    delete ui;
}

void SyringePumpTab::populateSyringeOptions() {
    auto populateMfg = [](QComboBox* combo) {
        for (const auto& entry : SYRINGE_MFGS) {
            combo->addItem(entry.name, entry.code);
        }
    };
    auto populateSpec = [](QComboBox* combo) {
        for (const auto& entry : SYRINGE_SPECS) {
            combo->addItem(entry.label, entry.code);
        }
    };
    populateMfg(ui->sampleMfgCombo);
    populateMfg(ui->sheathMfgCombo);
    populateSpec(ui->sampleSpecCombo);
    populateSpec(ui->sheathSpecCombo);
}

void SyringePumpTab::populateComPortList(int pumpIndex) {
    QComboBox* combo = (pumpIndex == 0) ? ui->sampleComPortCombo : ui->sheathComPortCombo;
    combo->clear();

    auto& pumpService = backend_.syringePump();
    PumpId id = static_cast<PumpId>(pumpIndex);

    if (pumpService.isConnected(id)) {
        int port = pumpService.getComPort(id);
        combo->addItem(QString("COM%1").arg(port), port);
        combo->setCurrentIndex(0);
        return;
    }

    std::vector<int> ports = backend::Tools::availableComPortNumbers();

    // Exclude ports in use by autofocus or the other pump
    int autofocusPort = -1;
    if (backend_.autofocus().isConnected()) {
        autofocusPort = backend_.autofocus().getComPort();
    }
    int otherPumpIdx = (pumpIndex == 0) ? 1 : 0;
    int otherPumpPort = -1;
    if (pumpService.isConnected(static_cast<PumpId>(otherPumpIdx))) {
        otherPumpPort = pumpService.getComPort(static_cast<PumpId>(otherPumpIdx));
    }

    for (int port : ports) {
        if (port == autofocusPort || port == otherPumpPort) {
            continue;
        }
        combo->addItem(QString("COM%1").arg(port), port);
    }
}

uint16_t SyringePumpTab::flowUnitFromCombo(int comboIndex) const {
    // 0 = uL/min (Modbus unit code 97), 1 = mL/min (Modbus unit code 99)
    return (comboIndex == 0) ? 97 : 99;
}

int SyringePumpTab::comboIndexFromFlowUnit(uint16_t unit) const {
    return (unit == 97) ? 0 : 1; // default to mL/min
}

// ---------------------------------------------------------------------------
// Connection slots
// ---------------------------------------------------------------------------
void SyringePumpTab::onConnectSample() {
    if (ui->sampleComPortCombo->count() == 0) return;
    int comPort = ui->sampleComPortCombo->currentData().toInt();
    int baudRate = ui->sampleBaudCombo->currentData().toInt();
    uint8_t addr = static_cast<uint8_t>(ui->sampleAddressSpinBox->value());

    bool ok = backend_.syringePump().connect(PumpId::Sample, comPort, baudRate, addr);
    if (ok) {
        saveConfig();
    } else {
        QMessageBox::warning(this, tr("Connection Failed"),
                            tr("Failed to connect to Sample pump on COM%1").arg(comPort));
    }
    updatePumpUI(0);
}

void SyringePumpTab::onDisconnectSample() {
    saveConfig();
    backend_.syringePump().disconnect(PumpId::Sample);
    updatePumpUI(0);
}

void SyringePumpTab::onConnectSheath() {
    if (ui->sheathComPortCombo->count() == 0) return;
    int comPort = ui->sheathComPortCombo->currentData().toInt();
    int baudRate = ui->sheathBaudCombo->currentData().toInt();
    uint8_t addr = static_cast<uint8_t>(ui->sheathAddressSpinBox->value());

    bool ok = backend_.syringePump().connect(PumpId::Sheath, comPort, baudRate, addr);
    if (ok) {
        saveConfig();
    } else {
        QMessageBox::warning(this, tr("Connection Failed"),
                            tr("Failed to connect to Sheath pump on COM%1").arg(comPort));
    }
    updatePumpUI(1);
}

void SyringePumpTab::onDisconnectSheath() {
    saveConfig();
    backend_.syringePump().disconnect(PumpId::Sheath);
    updatePumpUI(1);
}

// ---------------------------------------------------------------------------
// Control slots
// ---------------------------------------------------------------------------
void SyringePumpTab::onStartSample() {
    backend_.syringePump().start(PumpId::Sample);
}

void SyringePumpTab::onStopSample() {
    backend_.syringePump().stop(PumpId::Sample);
}

void SyringePumpTab::onStartSheath() {
    backend_.syringePump().start(PumpId::Sheath);
}

void SyringePumpTab::onStopSheath() {
    backend_.syringePump().stop(PumpId::Sheath);
}

void SyringePumpTab::onApplySample() {
    double rate = ui->sampleFlowRateSpinBox->value();
    uint16_t unit = flowUnitFromCombo(ui->sampleFlowUnitCombo->currentIndex());
    int dirIdx = ui->sampleDirectionCombo->currentIndex();

    backend_.syringePump().setFlowRate(PumpId::Sample, rate, unit);
    backend_.syringePump().setDirection(PumpId::Sample,
        dirIdx == 0 ? Direction::Infuse : Direction::Withdraw);

    // Apply syringe if changed
    uint16_t mfg = static_cast<uint16_t>(ui->sampleMfgCombo->currentData().toUInt());
    uint16_t spec = static_cast<uint16_t>(ui->sampleSpecCombo->currentData().toUInt());
    backend_.syringePump().setSyringe(PumpId::Sample, mfg, spec);

    saveConfig();
}

void SyringePumpTab::onApplySheath() {
    double rate = ui->sheathFlowRateSpinBox->value();
    uint16_t unit = flowUnitFromCombo(ui->sheathFlowUnitCombo->currentIndex());
    int dirIdx = ui->sheathDirectionCombo->currentIndex();

    backend_.syringePump().setFlowRate(PumpId::Sheath, rate, unit);
    backend_.syringePump().setDirection(PumpId::Sheath,
        dirIdx == 0 ? Direction::Infuse : Direction::Withdraw);

    uint16_t mfg = static_cast<uint16_t>(ui->sheathMfgCombo->currentData().toUInt());
    uint16_t spec = static_cast<uint16_t>(ui->sheathSpecCombo->currentData().toUInt());
    backend_.syringePump().setSyringe(PumpId::Sheath, mfg, spec);

    saveConfig();
}

void SyringePumpTab::onRefreshSamplePorts() {
    populateComPortList(0);
}

void SyringePumpTab::onRefreshSheathPorts() {
    populateComPortList(1);
}

// ---------------------------------------------------------------------------
// Status polling
// ---------------------------------------------------------------------------
void SyringePumpTab::onUpdateStatus() {
    auto& pumpService = backend_.syringePump();

    // Poll both pumps
    if (pumpService.isConnected(PumpId::Sample)) {
        pumpService.pollStatus(PumpId::Sample);
    }
    if (pumpService.isConnected(PumpId::Sheath)) {
        pumpService.pollStatus(PumpId::Sheath);
    }

    updatePumpUI(0);
    updatePumpUI(1);
}

// ---------------------------------------------------------------------------
// UI state management
// ---------------------------------------------------------------------------
void SyringePumpTab::updatePumpUI(int pumpIndex) {
    auto& pumpService = backend_.syringePump();
    PumpId id = static_cast<PumpId>(pumpIndex);
    auto status = pumpService.getStatus(id);
    bool connected = status.connected;

    if (pumpIndex == 0) {
        // Sample pump UI
        ui->sampleConnectBtn->setEnabled(!connected && ui->sampleComPortCombo->count() > 0);
        ui->sampleDisconnectBtn->setEnabled(connected);
        ui->sampleComPortCombo->setEnabled(!connected);
        ui->sampleRefreshBtn->setEnabled(!connected);
        ui->sampleBaudCombo->setEnabled(!connected);
        ui->sampleAddressSpinBox->setEnabled(!connected);
        ui->sampleFlowRateSpinBox->setEnabled(connected);
        ui->sampleFlowUnitCombo->setEnabled(connected);
        ui->sampleDirectionCombo->setEnabled(connected);
        ui->sampleStartBtn->setEnabled(connected);
        ui->sampleStopBtn->setEnabled(connected);
        ui->sampleApplyBtn->setEnabled(connected);
        ui->sampleMfgCombo->setEnabled(!connected);
        ui->sampleSpecCombo->setEnabled(!connected);

        if (connected) {
            ui->sampleStatusLabel->setText(runStatusToString(status.runStatus));
            ui->sampleFlowStatusLabel->setText(QString("%1").arg(status.currentFlowRate, 0, 'f', 4));
            ui->sampleVolumeLabel->setText(QString("%1").arg(status.accumulatedVolume, 0, 'f', 4));
        } else {
            ui->sampleStatusLabel->setText("Not connected");
            ui->sampleFlowStatusLabel->setText("--");
            ui->sampleVolumeLabel->setText("--");
        }
    } else {
        // Sheath pump UI
        ui->sheathConnectBtn->setEnabled(!connected && ui->sheathComPortCombo->count() > 0);
        ui->sheathDisconnectBtn->setEnabled(connected);
        ui->sheathComPortCombo->setEnabled(!connected);
        ui->sheathRefreshBtn->setEnabled(!connected);
        ui->sheathBaudCombo->setEnabled(!connected);
        ui->sheathAddressSpinBox->setEnabled(!connected);
        ui->sheathFlowRateSpinBox->setEnabled(connected);
        ui->sheathFlowUnitCombo->setEnabled(connected);
        ui->sheathDirectionCombo->setEnabled(connected);
        ui->sheathStartBtn->setEnabled(connected);
        ui->sheathStopBtn->setEnabled(connected);
        ui->sheathApplyBtn->setEnabled(connected);
        ui->sheathMfgCombo->setEnabled(!connected);
        ui->sheathSpecCombo->setEnabled(!connected);

        if (connected) {
            ui->sheathStatusLabel->setText(runStatusToString(status.runStatus));
            ui->sheathFlowStatusLabel->setText(QString("%1").arg(status.currentFlowRate, 0, 'f', 4));
            ui->sheathVolumeLabel->setText(QString("%1").arg(status.accumulatedVolume, 0, 'f', 4));
        } else {
            ui->sheathStatusLabel->setText("Not connected");
            ui->sheathFlowStatusLabel->setText("--");
            ui->sheathVolumeLabel->setText("--");
        }
    }
}

// ---------------------------------------------------------------------------
// Config persistence
// ---------------------------------------------------------------------------
QString SyringePumpTab::configPath() const {
    QSettings s;
    const QString external = s.value("Config/ExternalAppConfigPath").toString().trimmed();
    if (!external.isEmpty()) {
        return external;
    }
    return QDir(getUserConfigDir()).absoluteFilePath("config.json");
}

void SyringePumpTab::loadConfig() {
    QString path = configPath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    try {
        QByteArray data = file.readAll();
        json config = json::parse(data.constData(), data.constData() + data.size());

        // Sample pump config
        if (config.contains("pump_sample_com_port")) {
            int port = config["pump_sample_com_port"].get<int>();
            int idx = ui->sampleComPortCombo->findData(port);
            if (idx >= 0) ui->sampleComPortCombo->setCurrentIndex(idx);
        }
        if (config.contains("pump_sample_baud_rate")) {
            int baud = config["pump_sample_baud_rate"].get<int>();
            int idx = ui->sampleBaudCombo->findData(baud);
            if (idx >= 0) ui->sampleBaudCombo->setCurrentIndex(idx);
        }
        if (config.contains("pump_sample_address")) {
            ui->sampleAddressSpinBox->setValue(config["pump_sample_address"].get<int>());
        }
        if (config.contains("pump_sample_flow_rate")) {
            ui->sampleFlowRateSpinBox->setValue(config["pump_sample_flow_rate"].get<double>());
        }
        if (config.contains("pump_sample_flow_unit")) {
            ui->sampleFlowUnitCombo->setCurrentIndex(
                comboIndexFromFlowUnit(config["pump_sample_flow_unit"].get<uint16_t>()));
        }
        if (config.contains("pump_sample_direction")) {
            ui->sampleDirectionCombo->setCurrentIndex(config["pump_sample_direction"].get<int>());
        }
        if (config.contains("pump_sample_syringe_mfg")) {
            int idx = ui->sampleMfgCombo->findData(config["pump_sample_syringe_mfg"].get<uint16_t>());
            if (idx >= 0) ui->sampleMfgCombo->setCurrentIndex(idx);
        }
        if (config.contains("pump_sample_syringe_spec")) {
            int idx = ui->sampleSpecCombo->findData(config["pump_sample_syringe_spec"].get<uint16_t>());
            if (idx >= 0) ui->sampleSpecCombo->setCurrentIndex(idx);
        }

        // Sheath pump config
        if (config.contains("pump_sheath_com_port")) {
            int port = config["pump_sheath_com_port"].get<int>();
            int idx = ui->sheathComPortCombo->findData(port);
            if (idx >= 0) ui->sheathComPortCombo->setCurrentIndex(idx);
        }
        if (config.contains("pump_sheath_baud_rate")) {
            int baud = config["pump_sheath_baud_rate"].get<int>();
            int idx = ui->sheathBaudCombo->findData(baud);
            if (idx >= 0) ui->sheathBaudCombo->setCurrentIndex(idx);
        }
        if (config.contains("pump_sheath_address")) {
            ui->sheathAddressSpinBox->setValue(config["pump_sheath_address"].get<int>());
        }
        if (config.contains("pump_sheath_flow_rate")) {
            ui->sheathFlowRateSpinBox->setValue(config["pump_sheath_flow_rate"].get<double>());
        }
        if (config.contains("pump_sheath_flow_unit")) {
            ui->sheathFlowUnitCombo->setCurrentIndex(
                comboIndexFromFlowUnit(config["pump_sheath_flow_unit"].get<uint16_t>()));
        }
        if (config.contains("pump_sheath_direction")) {
            ui->sheathDirectionCombo->setCurrentIndex(config["pump_sheath_direction"].get<int>());
        }
        if (config.contains("pump_sheath_syringe_mfg")) {
            int idx = ui->sheathMfgCombo->findData(config["pump_sheath_syringe_mfg"].get<uint16_t>());
            if (idx >= 0) ui->sheathMfgCombo->setCurrentIndex(idx);
        }
        if (config.contains("pump_sheath_syringe_spec")) {
            int idx = ui->sheathSpecCombo->findData(config["pump_sheath_syringe_spec"].get<uint16_t>());
            if (idx >= 0) ui->sheathSpecCombo->setCurrentIndex(idx);
        }
    }
    catch (const std::exception& e) {
        SPDLOG_ERROR("SyringePumpTab: Failed to parse config.json: {}", e.what());
    }
}

void SyringePumpTab::saveConfig() {
    QString path = configPath();
    QFile file(path);
    if (!file.open(QIODevice::ReadWrite | QIODevice::Text)) {
        SPDLOG_WARN("SyringePumpTab: Failed to save config to {}", path.toStdString());
        return;
    }

    try {
        QByteArray data = file.readAll();
        json config;
        if (!data.isEmpty()) {
            config = json::parse(data.constData(), data.constData() + data.size());
        }

        // Sample pump
        if (ui->sampleComPortCombo->currentIndex() >= 0) {
            config["pump_sample_com_port"] = ui->sampleComPortCombo->currentData().toInt();
        }
        config["pump_sample_baud_rate"] = ui->sampleBaudCombo->currentData().toInt();
        config["pump_sample_address"] = ui->sampleAddressSpinBox->value();
        config["pump_sample_flow_rate"] = ui->sampleFlowRateSpinBox->value();
        config["pump_sample_flow_unit"] = flowUnitFromCombo(ui->sampleFlowUnitCombo->currentIndex());
        config["pump_sample_direction"] = ui->sampleDirectionCombo->currentIndex();
        config["pump_sample_syringe_mfg"] = ui->sampleMfgCombo->currentData().toUInt();
        config["pump_sample_syringe_spec"] = ui->sampleSpecCombo->currentData().toUInt();

        // Sheath pump
        if (ui->sheathComPortCombo->currentIndex() >= 0) {
            config["pump_sheath_com_port"] = ui->sheathComPortCombo->currentData().toInt();
        }
        config["pump_sheath_baud_rate"] = ui->sheathBaudCombo->currentData().toInt();
        config["pump_sheath_address"] = ui->sheathAddressSpinBox->value();
        config["pump_sheath_flow_rate"] = ui->sheathFlowRateSpinBox->value();
        config["pump_sheath_flow_unit"] = flowUnitFromCombo(ui->sheathFlowUnitCombo->currentIndex());
        config["pump_sheath_direction"] = ui->sheathDirectionCombo->currentIndex();
        config["pump_sheath_syringe_mfg"] = ui->sheathMfgCombo->currentData().toUInt();
        config["pump_sheath_syringe_spec"] = ui->sheathSpecCombo->currentData().toUInt();

        file.resize(0);
        QTextStream out(&file);
        out << QString::fromStdString(config.dump(4));
    }
    catch (const std::exception& e) {
        SPDLOG_ERROR("SyringePumpTab: Failed to save config.json: {}", e.what());
    }
}

} // namespace frontend

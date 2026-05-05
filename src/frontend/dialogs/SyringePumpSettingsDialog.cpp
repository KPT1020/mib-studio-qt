#include "frontend/dialogs/SyringePumpSettingsDialog.h"
#include "ui_SyringePumpSettingsDialog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QSettings>
#include <QTextStream>
#include <QSignalBlocker>

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

namespace {
    constexpr uint16_t VOL_UNIT_UL = 100;
    constexpr uint16_t VOL_UNIT_ML = 103;

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
} // namespace

SyringePumpSettingsDialog::SyringePumpSettingsDialog(backend::AppBackend& backend, QWidget* parent)
    : QDialog(parent), ui(new Ui::SyringePumpSettingsDialog), backend_(backend)
{
    ui->setupUi(this);

    // Syringe volume/target unit combo data
    ui->sampleSyringeUnitCombo->setItemData(0, VOL_UNIT_UL);
    ui->sampleSyringeUnitCombo->setItemData(1, VOL_UNIT_ML);
    ui->sheathSyringeUnitCombo->setItemData(0, VOL_UNIT_UL);
    ui->sheathSyringeUnitCombo->setItemData(1, VOL_UNIT_ML);
    ui->sampleTargetUnitCombo->setItemData(0, VOL_UNIT_UL);
    ui->sampleTargetUnitCombo->setItemData(1, VOL_UNIT_ML);
    ui->sheathTargetUnitCombo->setItemData(0, VOL_UNIT_UL);
    ui->sheathTargetUnitCombo->setItemData(1, VOL_UNIT_ML);

    populateComPorts();
    loadConfig();

    connect(ui->sampleRefreshBtn, &QPushButton::clicked, this, &SyringePumpSettingsDialog::onRefreshPorts);
    connect(ui->sheathRefreshBtn, &QPushButton::clicked, this, &SyringePumpSettingsDialog::onRefreshPorts);
    connect(ui->sampleScanAddrBtn, &QPushButton::clicked, this, &SyringePumpSettingsDialog::onScanSampleAddresses);
    connect(ui->sheathScanAddrBtn, &QPushButton::clicked, this, &SyringePumpSettingsDialog::onScanSheathAddresses);
    connect(ui->sampleApplyScanAddrBtn, &QPushButton::clicked, this, &SyringePumpSettingsDialog::onUseSampleScannedAddress);
    connect(ui->sheathApplyScanAddrBtn, &QPushButton::clicked, this, &SyringePumpSettingsDialog::onUseSheathScannedAddress);

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, [this]() { onApply(); accept(); });
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(ui->buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &SyringePumpSettingsDialog::onApply);
}

SyringePumpSettingsDialog::~SyringePumpSettingsDialog() {
    delete ui;
}

void SyringePumpSettingsDialog::onApply() {
    saveConfig();

    // If pumps are connected, apply syringe/target settings live
    auto& pump = backend_.syringePump();
    if (pump.isConnected(PumpId::Sample)) {
        pump.setSyringeVolume(PumpId::Sample,
            static_cast<uint16_t>(ui->sampleSyringeVolSpinBox->value()),
            ui->sampleSyringeUnitCombo->currentData().toUInt());
        pump.setTargetVolume(PumpId::Sample,
            static_cast<uint16_t>(ui->sampleTargetVolSpinBox->value()),
            ui->sampleTargetUnitCombo->currentData().toUInt());
        pump.setSyringeInnerDiameterMm(PumpId::Sample, ui->sampleInnerDiameterSpinBox->value());
    }
    if (pump.isConnected(PumpId::Sheath)) {
        pump.setSyringeVolume(PumpId::Sheath,
            static_cast<uint16_t>(ui->sheathSyringeVolSpinBox->value()),
            ui->sheathSyringeUnitCombo->currentData().toUInt());
        pump.setTargetVolume(PumpId::Sheath,
            static_cast<uint16_t>(ui->sheathTargetVolSpinBox->value()),
            ui->sheathTargetUnitCombo->currentData().toUInt());
        pump.setSyringeInnerDiameterMm(PumpId::Sheath, ui->sheathInnerDiameterSpinBox->value());
    }
}

void SyringePumpSettingsDialog::onRefreshPorts() {
    populateComPorts();
}

void SyringePumpSettingsDialog::onScanSampleAddresses() {
    scanAddresses(true);
}

void SyringePumpSettingsDialog::onScanSheathAddresses() {
    scanAddresses(false);
}

void SyringePumpSettingsDialog::onUseSampleScannedAddress() {
    if (ui->sampleScanResultCombo->currentIndex() < 0) return;
    ui->sampleAddressSpinBox->setValue(ui->sampleScanResultCombo->currentData().toInt());
}

void SyringePumpSettingsDialog::onUseSheathScannedAddress() {
    if (ui->sheathScanResultCombo->currentIndex() < 0) return;
    ui->sheathAddressSpinBox->setValue(ui->sheathScanResultCombo->currentData().toInt());
}

void SyringePumpSettingsDialog::scanAddresses(bool samplePump) {
    auto* comCombo = samplePump ? ui->sampleComPortCombo : ui->sheathComPortCombo;
    auto* baudSpin = samplePump ? ui->sampleBaudRateSpinBox : ui->sheathBaudRateSpinBox;
    auto* resultCombo = samplePump ? ui->sampleScanResultCombo : ui->sheathScanResultCombo;

    if (comCombo->currentIndex() < 0) {
        QMessageBox::warning(this, tr("Scan Failed"), tr("Select a COM port first."));
        return;
    }

    const int comPort = comCombo->currentData().toInt();
    const int baudRate = baudSpin->value();
    const auto found = backend_.syringePump().scanModbusAddresses(comPort, baudRate, 1, 8, 300);

    QSignalBlocker blocker(resultCombo);
    resultCombo->clear();
    for (uint8_t addr : found) {
        resultCombo->addItem(QString::number(addr), static_cast<int>(addr));
    }

    if (found.empty()) {
        QMessageBox::information(this, tr("Scan Complete"),
            tr("No pump address responded on COM%1 at %2 baud.").arg(comPort).arg(baudRate));
    } else {
        QMessageBox::information(this, tr("Scan Complete"),
            tr("Found %1 responsive address(es) on COM%2.").arg(found.size()).arg(comPort));
    }
}

void SyringePumpSettingsDialog::populateComPorts() {
    int prevSample = ui->sampleComPortCombo->currentData().toInt();
    int prevSheath = ui->sheathComPortCombo->currentData().toInt();
    ui->sampleComPortCombo->clear();
    ui->sheathComPortCombo->clear();

    std::vector<int> ports = backend::Tools::availableComPortNumbers();

    // Exclude ports in use by autofocus
    int autofocusPort = -1;
    if (backend_.autofocus().isConnected()) {
        autofocusPort = backend_.autofocus().getComPort();
    }

    for (int port : ports) {
        if (port == autofocusPort) continue;
        QString label = QString("COM%1").arg(port);
        ui->sampleComPortCombo->addItem(label, port);
        ui->sheathComPortCombo->addItem(label, port);
    }

    // Restore previous selections
    int idx = ui->sampleComPortCombo->findData(prevSample);
    if (idx >= 0) ui->sampleComPortCombo->setCurrentIndex(idx);
    idx = ui->sheathComPortCombo->findData(prevSheath);
    if (idx >= 0) ui->sheathComPortCombo->setCurrentIndex(idx);
}

QString SyringePumpSettingsDialog::configPath() const {
    QSettings s;
    const QString external = s.value("Config/ExternalAppConfigPath").toString().trimmed();
    if (!external.isEmpty()) return external;
    return QDir(getUserConfigDir()).absoluteFilePath("config.json");
}

void SyringePumpSettingsDialog::loadConfig() {
    QString path = configPath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    try {
        QByteArray data = file.readAll();
        json config = json::parse(data.constData(), data.constData() + data.size());

        if (config.contains("pump_sample_com_port")) {
            int idx = ui->sampleComPortCombo->findData(config["pump_sample_com_port"].get<int>());
            if (idx >= 0) ui->sampleComPortCombo->setCurrentIndex(idx);
        }
        if (config.contains("pump_sample_baud_rate"))
            ui->sampleBaudRateSpinBox->setValue(config["pump_sample_baud_rate"].get<int>());
        if (config.contains("pump_sample_address"))
            ui->sampleAddressSpinBox->setValue(config["pump_sample_address"].get<int>());
        if (config.contains("pump_sample_syringe_vol"))
            ui->sampleSyringeVolSpinBox->setValue(config["pump_sample_syringe_vol"].get<double>());
        if (config.contains("pump_sample_syringe_unit")) {
            int idx = ui->sampleSyringeUnitCombo->findData(config["pump_sample_syringe_unit"].get<uint16_t>());
            if (idx >= 0) ui->sampleSyringeUnitCombo->setCurrentIndex(idx);
        }
        if (config.contains("pump_sample_target_volume"))
            ui->sampleTargetVolSpinBox->setValue(config["pump_sample_target_volume"].get<double>());
        if (config.contains("pump_sample_target_unit")) {
            int idx = ui->sampleTargetUnitCombo->findData(config["pump_sample_target_unit"].get<uint16_t>());
            if (idx >= 0) ui->sampleTargetUnitCombo->setCurrentIndex(idx);
        }
        if (config.contains("pump_sample_run_until_stall"))
            ui->sampleRunUntilStallCheckBox->setChecked(config["pump_sample_run_until_stall"].get<bool>());
        if (config.contains("pump_sample_inner_diameter_mm"))
            ui->sampleInnerDiameterSpinBox->setValue(config["pump_sample_inner_diameter_mm"].get<double>());

        if (config.contains("pump_sheath_com_port")) {
            int idx = ui->sheathComPortCombo->findData(config["pump_sheath_com_port"].get<int>());
            if (idx >= 0) ui->sheathComPortCombo->setCurrentIndex(idx);
        }
        if (config.contains("pump_sheath_baud_rate"))
            ui->sheathBaudRateSpinBox->setValue(config["pump_sheath_baud_rate"].get<int>());
        if (config.contains("pump_sheath_address"))
            ui->sheathAddressSpinBox->setValue(config["pump_sheath_address"].get<int>());
        if (config.contains("pump_sheath_syringe_vol"))
            ui->sheathSyringeVolSpinBox->setValue(config["pump_sheath_syringe_vol"].get<double>());
        if (config.contains("pump_sheath_syringe_unit")) {
            int idx = ui->sheathSyringeUnitCombo->findData(config["pump_sheath_syringe_unit"].get<uint16_t>());
            if (idx >= 0) ui->sheathSyringeUnitCombo->setCurrentIndex(idx);
        }
        if (config.contains("pump_sheath_target_volume"))
            ui->sheathTargetVolSpinBox->setValue(config["pump_sheath_target_volume"].get<double>());
        if (config.contains("pump_sheath_target_unit")) {
            int idx = ui->sheathTargetUnitCombo->findData(config["pump_sheath_target_unit"].get<uint16_t>());
            if (idx >= 0) ui->sheathTargetUnitCombo->setCurrentIndex(idx);
        }
        if (config.contains("pump_sheath_run_until_stall"))
            ui->sheathRunUntilStallCheckBox->setChecked(config["pump_sheath_run_until_stall"].get<bool>());
        if (config.contains("pump_sheath_inner_diameter_mm"))
            ui->sheathInnerDiameterSpinBox->setValue(config["pump_sheath_inner_diameter_mm"].get<double>());
    }
    catch (const std::exception& e) {
        SPDLOG_ERROR("SyringePumpSettingsDialog: Failed to parse config: {}", e.what());
    }
}

void SyringePumpSettingsDialog::saveConfig() {
    QString path = configPath();
    QFile file(path);
    if (!file.open(QIODevice::ReadWrite | QIODevice::Text)) return;

    try {
        QByteArray data = file.readAll();
        json config;
        if (!data.isEmpty())
            config = json::parse(data.constData(), data.constData() + data.size());

        if (ui->sampleComPortCombo->currentIndex() >= 0)
            config["pump_sample_com_port"] = ui->sampleComPortCombo->currentData().toInt();
        config["pump_sample_baud_rate"] = ui->sampleBaudRateSpinBox->value();
        config["pump_sample_address"] = ui->sampleAddressSpinBox->value();
        config["pump_sample_syringe_vol"] = ui->sampleSyringeVolSpinBox->value();
        config["pump_sample_syringe_unit"] = ui->sampleSyringeUnitCombo->currentData().toUInt();
        config["pump_sample_target_volume"] = ui->sampleTargetVolSpinBox->value();
        config["pump_sample_target_unit"] = ui->sampleTargetUnitCombo->currentData().toUInt();
        config["pump_sample_run_until_stall"] = ui->sampleRunUntilStallCheckBox->isChecked();
        config["pump_sample_inner_diameter_mm"] = ui->sampleInnerDiameterSpinBox->value();

        if (ui->sheathComPortCombo->currentIndex() >= 0)
            config["pump_sheath_com_port"] = ui->sheathComPortCombo->currentData().toInt();
        config["pump_sheath_baud_rate"] = ui->sheathBaudRateSpinBox->value();
        config["pump_sheath_address"] = ui->sheathAddressSpinBox->value();
        config["pump_sheath_syringe_vol"] = ui->sheathSyringeVolSpinBox->value();
        config["pump_sheath_syringe_unit"] = ui->sheathSyringeUnitCombo->currentData().toUInt();
        config["pump_sheath_target_volume"] = ui->sheathTargetVolSpinBox->value();
        config["pump_sheath_target_unit"] = ui->sheathTargetUnitCombo->currentData().toUInt();
        config["pump_sheath_run_until_stall"] = ui->sheathRunUntilStallCheckBox->isChecked();
        config["pump_sheath_inner_diameter_mm"] = ui->sheathInnerDiameterSpinBox->value();

        file.resize(0);
        QTextStream out(&file);
        out << QString::fromStdString(config.dump(4));
    }
    catch (const std::exception& e) {
        SPDLOG_ERROR("SyringePumpSettingsDialog: Failed to save config: {}", e.what());
    }
}

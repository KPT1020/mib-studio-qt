#include "frontend/dialogs/SyringePumpSettingsDialog.h"
#include "ui_SyringePumpSettingsDialog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QSettings>
#include <QTextStream>

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

    // Syringe unit combo data
    ui->sampleSyringeUnitCombo->setItemData(0, VOL_UNIT_UL);
    ui->sampleSyringeUnitCombo->setItemData(1, VOL_UNIT_ML);
    ui->sheathSyringeUnitCombo->setItemData(0, VOL_UNIT_UL);
    ui->sheathSyringeUnitCombo->setItemData(1, VOL_UNIT_ML);

    populateComPorts();
    loadConfig();

    connect(ui->sampleRefreshBtn, &QPushButton::clicked, this, &SyringePumpSettingsDialog::onRefreshPorts);
    connect(ui->sheathRefreshBtn, &QPushButton::clicked, this, &SyringePumpSettingsDialog::onRefreshPorts);

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, [this]() { onApply(); accept(); });
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(ui->buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &SyringePumpSettingsDialog::onApply);
}

SyringePumpSettingsDialog::~SyringePumpSettingsDialog() {
    delete ui;
}

void SyringePumpSettingsDialog::onApply() {
    saveConfig();

    // If pumps are connected, apply syringe volume live
    auto& pump = backend_.syringePump();
    if (pump.isConnected(PumpId::Sample)) {
        pump.setSyringeVolume(PumpId::Sample,
            static_cast<uint16_t>(ui->sampleSyringeVolSpinBox->value()),
            ui->sampleSyringeUnitCombo->currentData().toUInt());
    }
    if (pump.isConnected(PumpId::Sheath)) {
        pump.setSyringeVolume(PumpId::Sheath,
            static_cast<uint16_t>(ui->sheathSyringeVolSpinBox->value()),
            ui->sheathSyringeUnitCombo->currentData().toUInt());
    }
}

void SyringePumpSettingsDialog::onRefreshPorts() {
    populateComPorts();
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
        if (config.contains("pump_sample_syringe_vol"))
            ui->sampleSyringeVolSpinBox->setValue(config["pump_sample_syringe_vol"].get<double>());
        if (config.contains("pump_sample_syringe_unit")) {
            int idx = ui->sampleSyringeUnitCombo->findData(config["pump_sample_syringe_unit"].get<uint16_t>());
            if (idx >= 0) ui->sampleSyringeUnitCombo->setCurrentIndex(idx);
        }

        if (config.contains("pump_sheath_com_port")) {
            int idx = ui->sheathComPortCombo->findData(config["pump_sheath_com_port"].get<int>());
            if (idx >= 0) ui->sheathComPortCombo->setCurrentIndex(idx);
        }
        if (config.contains("pump_sheath_syringe_vol"))
            ui->sheathSyringeVolSpinBox->setValue(config["pump_sheath_syringe_vol"].get<double>());
        if (config.contains("pump_sheath_syringe_unit")) {
            int idx = ui->sheathSyringeUnitCombo->findData(config["pump_sheath_syringe_unit"].get<uint16_t>());
            if (idx >= 0) ui->sheathSyringeUnitCombo->setCurrentIndex(idx);
        }
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
        config["pump_sample_baud_rate"] = 115200;
        config["pump_sample_address"] = 1;
        config["pump_sample_syringe_vol"] = ui->sampleSyringeVolSpinBox->value();
        config["pump_sample_syringe_unit"] = ui->sampleSyringeUnitCombo->currentData().toUInt();

        if (ui->sheathComPortCombo->currentIndex() >= 0)
            config["pump_sheath_com_port"] = ui->sheathComPortCombo->currentData().toInt();
        config["pump_sheath_baud_rate"] = 115200;
        config["pump_sheath_address"] = 1;
        config["pump_sheath_syringe_vol"] = ui->sheathSyringeVolSpinBox->value();
        config["pump_sheath_syringe_unit"] = ui->sheathSyringeUnitCombo->currentData().toUInt();

        file.resize(0);
        QTextStream out(&file);
        out << QString::fromStdString(config.dump(4));
    }
    catch (const std::exception& e) {
        SPDLOG_ERROR("SyringePumpSettingsDialog: Failed to save config: {}", e.what());
    }
}

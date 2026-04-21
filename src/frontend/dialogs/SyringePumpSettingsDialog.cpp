#include "frontend/dialogs/SyringePumpSettingsDialog.h"
#include "ui_SyringePumpSettingsDialog.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTextStream>
#include <QPushButton>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#define NOMINMAX
#include <shlobj.h>
#include <windows.h>
#endif

#include "backend/Tools.h"
#include "backend/services/SyringePumpService.h"

using json = nlohmann::json;
using backend::services::SyringePumpService;
namespace {
constexpr uint16_t VOL_UNIT_UL = 100;
constexpr uint16_t VOL_UNIT_ML = 103;

QString getUserConfigDir() {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString appDirLower = appDir.toLower();
#ifdef _WIN32
    if (appDirLower.contains("program files") || appDirLower.contains("program files (x86)")) {
        char appDataPath[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathA(
                nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appDataPath))) {
            const QString userConfigDir = QDir(QString::fromStdString(std::string(appDataPath) +
                                                                       "\\MIB_Studio_Qt\\include"))
                                              .absolutePath();
            QDir().mkpath(userConfigDir);
            return userConfigDir;
        }
    }
#endif
    return QDir(appDir).absoluteFilePath("../include");
}

QString normalizePortName(const QString& name) {
    return name.trimmed().toUpper();
}

void migrateLegacyConfig(json& config) {
    if (config.contains("pump_ports") && config["pump_ports"].is_array() && !config["pump_ports"].empty()) {
        return;
    }

    auto parseLegacyPort = [&](const char* intKey, const char* nameKey) -> std::string {
        if (config.contains(nameKey) && config[nameKey].is_string()) {
            return config[nameKey].get<std::string>();
        }
#ifdef _WIN32
        if (config.contains(intKey) && config[intKey].is_number_integer()) {
            const int comPort = config[intKey].get<int>();
            if (comPort > 0) {
                return "COM" + std::to_string(comPort);
            }
        }
#else
        (void)intKey;
#endif
        return {};
    };

    json sample = {
        {"name", "Sample"},
        {"port_name", parseLegacyPort("pump_sample_com_port", "pump_sample_port_name")},
        {"baud_rate", config.value("pump_sample_baud_rate", 115200)},
        {"address", config.value("pump_sample_address", 1)},
        {"flow_rate", config.value("pump_sample_flow_rate", 0.0)},
        {"flow_rate_unit", config.value("pump_sample_flow_unit", 100)},
        {"direction", config.value("pump_sample_direction", 0)},
        {"syringe_volume", config.value("pump_sample_syringe_vol", 100.0)},
        {"syringe_volume_unit", config.value("pump_sample_syringe_unit", 100)},
    };
    json sheath = {
        {"name", "Sheath"},
        {"port_name", parseLegacyPort("pump_sheath_com_port", "pump_sheath_port_name")},
        {"baud_rate", config.value("pump_sheath_baud_rate", 115200)},
        {"address", config.value("pump_sheath_address", 1)},
        {"flow_rate", config.value("pump_sheath_flow_rate", 0.0)},
        {"flow_rate_unit", config.value("pump_sheath_flow_unit", 100)},
        {"direction", config.value("pump_sheath_direction", 0)},
        {"syringe_volume", config.value("pump_sheath_syringe_vol", 100.0)},
        {"syringe_volume_unit", config.value("pump_sheath_syringe_unit", 100)},
    };
    config["pump_ports"] = json::array({sample, sheath});
}
} // namespace

SyringePumpSettingsDialog::SyringePumpSettingsDialog(
    SyringePumpService& pumpService,
    ReservedPortNamesProvider reservedPortNamesProvider,
    QWidget* parent)
    : QDialog(parent),
      ui(new Ui::SyringePumpSettingsDialog),
      pumpService_(pumpService),
      reservedPortNamesProvider_(std::move(reservedPortNamesProvider)) {
    ui->setupUi(this);

    ui->sampleSyringeUnitCombo->setItemData(0, VOL_UNIT_UL);
    ui->sampleSyringeUnitCombo->setItemData(1, VOL_UNIT_ML);
    ui->sheathSyringeUnitCombo->setItemData(0, VOL_UNIT_UL);
    ui->sheathSyringeUnitCombo->setItemData(1, VOL_UNIT_ML);

    populateComPorts();
    loadConfig();

    connect(ui->sampleRefreshBtn, &QPushButton::clicked, this, &SyringePumpSettingsDialog::onRefreshPorts);
    connect(ui->sheathRefreshBtn, &QPushButton::clicked, this, &SyringePumpSettingsDialog::onRefreshPorts);
    connect(ui->buttonBox->button(QDialogButtonBox::Apply),
            &QPushButton::clicked,
            this,
            &SyringePumpSettingsDialog::onApply);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        onApply();
        accept();
    });
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

SyringePumpSettingsDialog::~SyringePumpSettingsDialog() {
    delete ui;
}

void SyringePumpSettingsDialog::onApply() {
    saveConfig();

    const auto handles = pumpService_.pumpHandles();
    if (!handles.empty() && pumpService_.isConnected(handles[0])) {
        pumpService_.setSyringeVolume(handles[0],
                                      static_cast<uint16_t>(ui->sampleSyringeVolSpinBox->value()),
                                      static_cast<uint16_t>(ui->sampleSyringeUnitCombo->currentData().toUInt()));
    }
    if (handles.size() > 1 && pumpService_.isConnected(handles[1])) {
        pumpService_.setSyringeVolume(handles[1],
                                      static_cast<uint16_t>(ui->sheathSyringeVolSpinBox->value()),
                                      static_cast<uint16_t>(ui->sheathSyringeUnitCombo->currentData().toUInt()));
    }
}

void SyringePumpSettingsDialog::onRefreshPorts() {
    populateComPorts();
}

QString SyringePumpSettingsDialog::configPath() const {
    QSettings s;
    const QString external = s.value("Config/ExternalAppConfigPath").toString().trimmed();
    if (!external.isEmpty()) {
        return external;
    }
    return QDir(getUserConfigDir()).absoluteFilePath("config.json");
}

void SyringePumpSettingsDialog::populateComPorts() {
    const QString prevSample = ui->sampleComPortCombo->currentData().toString();
    const QString prevSheath = ui->sheathComPortCombo->currentData().toString();
    ui->sampleComPortCombo->clear();
    ui->sheathComPortCombo->clear();

    QStringList reserved = reservedPortNamesProvider_ ? reservedPortNamesProvider_() : QStringList{};
    for (auto& item : reserved) {
        item = normalizePortName(item);
    }
    reserved.removeAll(QString{});
    reserved.removeDuplicates();

    const auto handles = pumpService_.pumpHandles();
    const QString currentSamplePort =
        handles.empty() ? QString{} : normalizePortName(pumpService_.getPortName(handles[0]));
    const QString currentSheathPort =
        handles.size() > 1 ? normalizePortName(pumpService_.getPortName(handles[1])) : QString{};

    auto fill = [&](QComboBox* combo, const QString& keepPort) {
        combo->addItem(tr("(Not set)"), QString{});
        for (const auto& port : backend::Tools::availableSerialPortNames()) {
            const QString qPort = QString::fromStdString(port);
            const QString normalized = normalizePortName(qPort);
            if (!keepPort.isEmpty() && normalized == keepPort) {
                combo->addItem(qPort, qPort);
                continue;
            }
            if (!reserved.contains(normalized)) {
                combo->addItem(qPort, qPort);
            }
        }
    };

    fill(ui->sampleComPortCombo, currentSamplePort);
    fill(ui->sheathComPortCombo, currentSheathPort);

    int idx = ui->sampleComPortCombo->findData(prevSample);
    if (idx >= 0) {
        ui->sampleComPortCombo->setCurrentIndex(idx);
    }
    idx = ui->sheathComPortCombo->findData(prevSheath);
    if (idx >= 0) {
        ui->sheathComPortCombo->setCurrentIndex(idx);
    }
}

void SyringePumpSettingsDialog::loadConfig() {
    QFile file(configPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    try {
        const QByteArray data = file.readAll();
        json config = json::parse(data.constData(), data.constData() + data.size());
        migrateLegacyConfig(config);
        if (!config.contains("pump_ports") || !config["pump_ports"].is_array()) {
            return;
        }

        const auto& pumps = config["pump_ports"];
        if (!pumps.empty()) {
            const auto& sample = pumps[0];
            const QString samplePort = QString::fromStdString(sample.value("port_name", std::string{}));
            int idx = ui->sampleComPortCombo->findData(samplePort);
            if (idx >= 0) {
                ui->sampleComPortCombo->setCurrentIndex(idx);
            }
            ui->sampleSyringeVolSpinBox->setValue(sample.value("syringe_volume", 100.0));
            idx = ui->sampleSyringeUnitCombo->findData(sample.value("syringe_volume_unit", static_cast<int>(VOL_UNIT_UL)));
            if (idx >= 0) {
                ui->sampleSyringeUnitCombo->setCurrentIndex(idx);
            }
        }
        if (pumps.size() > 1) {
            const auto& sheath = pumps[1];
            const QString sheathPort = QString::fromStdString(sheath.value("port_name", std::string{}));
            int idx = ui->sheathComPortCombo->findData(sheathPort);
            if (idx >= 0) {
                ui->sheathComPortCombo->setCurrentIndex(idx);
            }
            ui->sheathSyringeVolSpinBox->setValue(sheath.value("syringe_volume", 100.0));
            idx = ui->sheathSyringeUnitCombo->findData(sheath.value("syringe_volume_unit", static_cast<int>(VOL_UNIT_UL)));
            if (idx >= 0) {
                ui->sheathSyringeUnitCombo->setCurrentIndex(idx);
            }
        }
    } catch (const std::exception& e) {
        SPDLOG_ERROR("SyringePumpSettingsDialog: Failed to parse config: {}", e.what());
    }
}

void SyringePumpSettingsDialog::saveConfig() {
    QFile file(configPath());
    if (!file.open(QIODevice::ReadWrite | QIODevice::Text)) {
        return;
    }

    try {
        const QByteArray data = file.readAll();
        json config = json::object();
        if (!data.isEmpty()) {
            config = json::parse(data.constData(), data.constData() + data.size());
        }
        migrateLegacyConfig(config);

        if (!config.contains("pump_ports") || !config["pump_ports"].is_array()) {
            config["pump_ports"] = json::array();
        }
        auto& pumps = config["pump_ports"];
        while (pumps.size() < 2) {
            pumps.push_back(json::object());
        }

        pumps[0]["name"] = "Sample";
        pumps[0]["port_name"] = ui->sampleComPortCombo->currentData().toString().toStdString();
        pumps[0]["baud_rate"] = 115200;
        pumps[0]["address"] = 1;
        pumps[0]["syringe_volume"] = ui->sampleSyringeVolSpinBox->value();
        pumps[0]["syringe_volume_unit"] = ui->sampleSyringeUnitCombo->currentData().toUInt();
        pumps[0]["flow_rate"] = pumps[0].value("flow_rate", 0.0);
        pumps[0]["flow_rate_unit"] = pumps[0].value("flow_rate_unit", 100);
        pumps[0]["direction"] = pumps[0].value("direction", 0);

        pumps[1]["name"] = "Sheath";
        pumps[1]["port_name"] = ui->sheathComPortCombo->currentData().toString().toStdString();
        pumps[1]["baud_rate"] = 115200;
        pumps[1]["address"] = 1;
        pumps[1]["syringe_volume"] = ui->sheathSyringeVolSpinBox->value();
        pumps[1]["syringe_volume_unit"] = ui->sheathSyringeUnitCombo->currentData().toUInt();
        pumps[1]["flow_rate"] = pumps[1].value("flow_rate", 0.0);
        pumps[1]["flow_rate_unit"] = pumps[1].value("flow_rate_unit", 100);
        pumps[1]["direction"] = pumps[1].value("direction", 0);

        file.resize(0);
        QTextStream out(&file);
        out << QString::fromStdString(config.dump(4));
    } catch (const std::exception& e) {
        SPDLOG_ERROR("SyringePumpSettingsDialog: Failed to save config: {}", e.what());
    }
}

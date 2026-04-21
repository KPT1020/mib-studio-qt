#include "frontend/tabs/SyringePumpTab.h"

#include "ui_SyringePumpTab.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QSettings>
#include <QTextStream>
#include <QVariant>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "backend/Tools.h"
#include "backend/services/SyringePumpService.h"
#include "frontend/widgets/PumpRowWidget.h"

#ifdef _WIN32
#define NOMINMAX
#include <shlobj.h>
#include <windows.h>
#endif

using json = nlohmann::json;
using backend::services::SyringePumpService;

namespace frontend {

namespace {
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

QString normalizePortName(const QString& value) {
    return value.trimmed().toUpper();
}
} // namespace

SyringePumpTab::SyringePumpTab(SyringePumpService& pumpService,
                               ReservedPortNamesProvider reservedPortNamesProvider,
                               QWidget* parent)
    : QWidget(parent),
      ui(new Ui::SyringePumpTab),
      pumpService_(pumpService),
      reservedPortNamesProvider_(std::move(reservedPortNamesProvider)) {
    ui->setupUi(this);
    rowsLayout_ = ui->pumpRowsLayout;

    connect(ui->addPumpBtn, &QPushButton::clicked, this, &SyringePumpTab::onAddPump);
    connect(ui->removePumpBtn, &QPushButton::clicked, this, &SyringePumpTab::onRemoveLastPump);

    loadConfig();
    rebuildPumpRows();

    statusUpdateTimer_ = new QTimer(this);
    statusUpdateTimer_->setInterval(500);
    connect(statusUpdateTimer_, &QTimer::timeout, this, &SyringePumpTab::onUpdateStatus);
    statusUpdateTimer_->start();

    connect(qApp, &QApplication::aboutToQuit, this, [this]() { saveConfig(); });
}

SyringePumpTab::~SyringePumpTab() {
    delete ui;
}

void SyringePumpTab::ensureMinimumPumpCount(size_t minCount, const QStringList& defaultNames) {
    while (pumpService_.pumpCount() < minCount) {
        const size_t idx = pumpService_.pumpCount();
        const QString name = idx < static_cast<size_t>(defaultNames.size())
                                 ? defaultNames[static_cast<int>(idx)]
                                 : QStringLiteral("Pump %1").arg(static_cast<int>(idx + 1));
        pumpService_.addPump(name);
    }
}

void SyringePumpTab::rebuildPumpRows() {
    QLayoutItem* item = nullptr;
    while ((item = rowsLayout_->takeAt(0)) != nullptr) {
        if (QWidget* w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }
    pumpRows_.clear();

    const auto handles = pumpService_.pumpHandles();
    for (const auto handle : handles) {
        auto row = std::make_unique<PumpRowWidget>(this);
        row->setProperty("pumpHandle", QVariant::fromValue<qulonglong>(handle));
        wirePumpRow(row.get());
        rowsLayout_->addWidget(row.get());
        pumpRows_.push_back(std::move(row));
    }
    rowsLayout_->addStretch();

    ui->removePumpBtn->setEnabled(handles.size() > 1);
    for (auto& row : pumpRows_) {
        updatePumpUI(row.get());
        populatePortChoices(row.get());
    }
}

void SyringePumpTab::wirePumpRow(PumpRowWidget* rowWidget) {
    const auto handle =
        static_cast<SyringePumpService::PumpHandle>(rowWidget->property("pumpHandle").toULongLong());

    connect(rowWidget, &PumpRowWidget::connectRequested, this, [this, rowWidget]() {
        if (!connectPumpFromConfig(rowWidget)) {
            const auto h = static_cast<SyringePumpService::PumpHandle>(
                rowWidget->property("pumpHandle").toULongLong());
            const auto cfg = pumpService_.getConfig(h);
            QMessageBox::warning(
                this,
                tr("Connection Failed"),
                tr("Failed to connect %1 on %2 addr=%3").arg(cfg.name).arg(cfg.portName).arg(cfg.modbusAddress));
        }
        updatePumpUI(rowWidget);
    });

    connect(rowWidget, &PumpRowWidget::disconnectRequested, this, [this, rowWidget]() {
        const auto h = static_cast<SyringePumpService::PumpHandle>(
            rowWidget->property("pumpHandle").toULongLong());
        pumpService_.disconnect(h);
        updatePumpUI(rowWidget);
    });

    connect(rowWidget, &PumpRowWidget::startRequested, this, [this, rowWidget]() {
        const auto h = static_cast<SyringePumpService::PumpHandle>(
            rowWidget->property("pumpHandle").toULongLong());
        pumpService_.start(h);
    });

    connect(rowWidget, &PumpRowWidget::stopRequested, this, [this, rowWidget]() {
        const auto h = static_cast<SyringePumpService::PumpHandle>(
            rowWidget->property("pumpHandle").toULongLong());
        pumpService_.stop(h);
    });

    connect(rowWidget, &PumpRowWidget::purgeRequested, this, [this, rowWidget](SyringePumpService::Direction dir) {
        const auto h = static_cast<SyringePumpService::PumpHandle>(
            rowWidget->property("pumpHandle").toULongLong());
        pumpService_.purge(h, dir);
    });

    connect(rowWidget, &PumpRowWidget::stopPurgeRequested, this, [this, rowWidget]() {
        const auto h = static_cast<SyringePumpService::PumpHandle>(
            rowWidget->property("pumpHandle").toULongLong());
        pumpService_.stopPurge(h);
    });

    connect(rowWidget, &PumpRowWidget::applyRequested, this, [this, rowWidget]() {
        const auto h = static_cast<SyringePumpService::PumpHandle>(
            rowWidget->property("pumpHandle").toULongLong());
        if (!pumpService_.isConnected(h)) {
            return;
        }
        const auto state = rowWidget->viewState();
        pumpService_.setFlowRate(h, state.flowRate, state.flowRateUnit);
        pumpService_.setDirection(h, state.direction);
        saveConfig();
    });

    connect(rowWidget, &PumpRowWidget::settingsChanged, this, [this, rowWidget]() {
        const auto h = static_cast<SyringePumpService::PumpHandle>(
            rowWidget->property("pumpHandle").toULongLong());
        const auto state = rowWidget->viewState();
        auto cfg = pumpService_.getConfig(h);
        const uint16_t prevSyringeVolume = cfg.syringeVolume;
        const uint16_t prevSyringeUnit = cfg.syringeVolumeUnit;
        cfg.portName = state.portName;
        cfg.baudRate = state.baudRate;
        cfg.modbusAddress = state.modbusAddress;
        cfg.syringeVolume = state.syringeVolume;
        cfg.syringeVolumeUnit = state.syringeVolumeUnit;
        pumpService_.setConfig(h, cfg);
        if (pumpService_.isConnected(h) &&
            (prevSyringeVolume != cfg.syringeVolume || prevSyringeUnit != cfg.syringeVolumeUnit)) {
            pumpService_.setSyringeVolume(h, cfg.syringeVolume, cfg.syringeVolumeUnit);
        }
        // Refresh port lists on sibling rows so the selected port is reserved.
        for (auto& sibling : pumpRows_) {
            if (sibling.get() != rowWidget) {
                populatePortChoices(sibling.get());
            }
        }
        saveConfig();
    });

    connect(rowWidget, &PumpRowWidget::portRefreshRequested, this, [this, rowWidget]() {
        populatePortChoices(rowWidget);
    });

    connect(rowWidget, &PumpRowWidget::nameChanged, this, [this, handle](const QString& name) {
        pumpService_.setPumpName(handle, name);
        saveConfig();
    });

    connect(rowWidget, &PumpRowWidget::removeRequested, this, [this, handle]() {
        if (pumpService_.pumpCount() <= 1) {
            return;
        }
        pumpService_.disconnect(handle);
        pumpService_.removePump(handle);
        rebuildPumpRows();
        saveConfig();
    });
}

QStringList SyringePumpTab::reservedPortNamesExcluding(SyringePumpService::PumpHandle selfHandle) const {
    QStringList reserved;
    if (reservedPortNamesProvider_) {
        reserved = reservedPortNamesProvider_();
    }
    for (const auto handle : pumpService_.pumpHandles()) {
        if (handle == selfHandle) {
            continue;
        }
        reserved << normalizePortName(pumpService_.getPortName(handle));
    }
    reserved.removeAll(QString{});
    reserved.removeDuplicates();
    return reserved;
}

void SyringePumpTab::populatePortChoices(PumpRowWidget* rowWidget) const {
    const auto handle =
        static_cast<SyringePumpService::PumpHandle>(rowWidget->property("pumpHandle").toULongLong());
    const QString currentPort = pumpService_.getPortName(handle);
    const auto reserved = reservedPortNamesExcluding(handle);

    QStringList available;
    for (const auto& port : backend::Tools::availableSerialPortNames()) {
        const QString qPort = QString::fromStdString(port);
        const QString normalized = normalizePortName(qPort);
        if (reserved.contains(normalized)) {
            continue;
        }
        available << qPort;
    }
    rowWidget->setPortChoices(available, currentPort);
}

void SyringePumpTab::updatePumpUI(PumpRowWidget* rowWidget) {
    const auto handle =
        static_cast<SyringePumpService::PumpHandle>(rowWidget->property("pumpHandle").toULongLong());
    const auto status = pumpService_.getStatus(handle);
    const auto cfg = pumpService_.getConfig(handle);

    rowWidget->setPumpName(cfg.name);

    PumpRowWidget::ViewState state;
    state.portName = cfg.portName;
    state.baudRate = cfg.baudRate;
    state.modbusAddress = cfg.modbusAddress;
    state.flowRate = cfg.flowRate;
    state.flowRateUnit = cfg.flowRateUnit;
    state.direction = cfg.direction;
    state.syringeVolume = cfg.syringeVolume;
    state.syringeVolumeUnit = cfg.syringeVolumeUnit;
    rowWidget->setViewState(state);

    rowWidget->setConnected(status.connected);
    rowWidget->setStatus(status);
    rowWidget->setRemoveEnabled(pumpService_.pumpCount() > 1);
}

bool SyringePumpTab::connectPumpFromConfig(PumpRowWidget* rowWidget) {
    const auto handle =
        static_cast<SyringePumpService::PumpHandle>(rowWidget->property("pumpHandle").toULongLong());
    const auto cfg = pumpService_.getConfig(handle);
    if (cfg.portName.trimmed().isEmpty()) {
        QMessageBox::warning(this,
                             tr("Connection Failed"),
                             tr("No serial port configured. Open Settings > Syringe Pump Settings."));
        return false;
    }
    const auto reserved = reservedPortNamesExcluding(handle);
    if (reserved.contains(normalizePortName(cfg.portName))) {
        QMessageBox::warning(this,
                             tr("Connection Failed"),
                             tr("Serial port %1 is currently reserved by another device.").arg(cfg.portName));
        return false;
    }
    return pumpService_.connect(handle, cfg.portName, cfg.baudRate, cfg.modbusAddress);
}

bool SyringePumpTab::loadJsonConfig(json& config) const {
    QFile file(configPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    const QByteArray data = file.readAll();
    if (data.isEmpty()) {
        config = json::object();
        return true;
    }
    config = json::parse(data.constData(), data.constData() + data.size());
    return true;
}

bool SyringePumpTab::writeJsonConfig(const json& config) const {
    QFile file(configPath());
    if (!file.open(QIODevice::ReadWrite | QIODevice::Text)) {
        return false;
    }
    file.resize(0);
    QTextStream out(&file);
    out << QString::fromStdString(config.dump(4));
    return true;
}

json SyringePumpTab::readOrCreateConfig() const {
    json config = json::object();
    (void)loadJsonConfig(config);
    if (!config.is_object()) {
        config = json::object();
    }
    return config;
}

json SyringePumpTab::defaultPumpJson(const QString& name, int defaultAddress) const {
    return json{
        {"name", name.toStdString()},
        {"port_name", ""},
        {"baud_rate", 115200},
        {"address", defaultAddress},
        {"flow_rate", 0.0},
        {"flow_rate_unit", 100},
        {"direction", 0},
        {"syringe_volume", 100},
        {"syringe_volume_unit", 100},
    };
}

void SyringePumpTab::migrateLegacyConfig(json& config) const {
    if (config.contains("pump_ports") && config["pump_ports"].is_array() && !config["pump_ports"].empty()) {
        return;
    }

    json ports = json::array();
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

    json sample = defaultPumpJson(QStringLiteral("Sample"), 1);
    sample["port_name"] = parseLegacyPort("pump_sample_com_port", "pump_sample_port_name");
    sample["baud_rate"] = config.value("pump_sample_baud_rate", 115200);
    sample["address"] = config.value("pump_sample_address", 1);
    sample["flow_rate"] = config.value("pump_sample_flow_rate", 0.0);
    sample["flow_rate_unit"] = config.value("pump_sample_flow_unit", 100);
    sample["direction"] = config.value("pump_sample_direction", 0);
    sample["syringe_volume"] = config.value("pump_sample_syringe_vol", 100.0);
    sample["syringe_volume_unit"] = config.value("pump_sample_syringe_unit", 100);

    json sheath = defaultPumpJson(QStringLiteral("Sheath"), 1);
    sheath["port_name"] = parseLegacyPort("pump_sheath_com_port", "pump_sheath_port_name");
    sheath["baud_rate"] = config.value("pump_sheath_baud_rate", 115200);
    sheath["address"] = config.value("pump_sheath_address", 1);
    sheath["flow_rate"] = config.value("pump_sheath_flow_rate", 0.0);
    sheath["flow_rate_unit"] = config.value("pump_sheath_flow_unit", 100);
    sheath["direction"] = config.value("pump_sheath_direction", 0);
    sheath["syringe_volume"] = config.value("pump_sheath_syringe_vol", 100.0);
    sheath["syringe_volume_unit"] = config.value("pump_sheath_syringe_unit", 100);

    ports.push_back(sample);
    ports.push_back(sheath);
    config["pump_ports"] = ports;
}

void SyringePumpTab::loadConfig() {
    json config = readOrCreateConfig();
    migrateLegacyConfig(config);

    const bool hasPumpArray = config.contains("pump_ports") && config["pump_ports"].is_array() &&
                              !config["pump_ports"].empty();
    if (!hasPumpArray) {
        ensureMinimumPumpCount(1, {QStringLiteral("Pump 1")});
        return;
    }

    pumpService_.clearPumps();
    const auto& pumpArray = config["pump_ports"];
    for (size_t i = 0; i < pumpArray.size(); ++i) {
        const auto& node = pumpArray[i];
        if (!node.is_object()) {
            continue;
        }
        const QString defaultName = (i == 0) ? QStringLiteral("Sample")
                                             : (i == 1) ? QStringLiteral("Sheath")
                                                        : QStringLiteral("Pump %1").arg(static_cast<int>(i + 1));
        const QString name = QString::fromStdString(node.value("name", defaultName.toStdString()));
        const auto handle = pumpService_.addPump(name);
        auto pumpCfg = pumpService_.getConfig(handle);
        pumpCfg.name = name;
        pumpCfg.portName = QString::fromStdString(node.value("port_name", std::string{}));
        pumpCfg.baudRate = node.value("baud_rate", 115200);
        pumpCfg.modbusAddress = static_cast<uint8_t>(node.value("address", 1));
        pumpCfg.flowRate = node.value("flow_rate", 0.0);
        pumpCfg.flowRateUnit = static_cast<uint16_t>(node.value("flow_rate_unit", 100));
        pumpCfg.direction = node.value("direction", 0) == 0 ? SyringePumpService::Direction::Infuse
                                                             : SyringePumpService::Direction::Withdraw;
        pumpCfg.syringeVolume = static_cast<uint16_t>(node.value("syringe_volume", 100));
        pumpCfg.syringeVolumeUnit = static_cast<uint16_t>(node.value("syringe_volume_unit", 100));
        pumpService_.setConfig(handle, pumpCfg);
    }

    ensureMinimumPumpCount(1, {QStringLiteral("Pump 1")});
}

void SyringePumpTab::saveConfig() {
    json config = readOrCreateConfig();
    json pumpArray = json::array();
    for (const auto handle : pumpService_.pumpHandles()) {
        const auto cfg = pumpService_.getConfig(handle);
        pumpArray.push_back(json{
            {"name", cfg.name.toStdString()},
            {"port_name", cfg.portName.toStdString()},
            {"baud_rate", cfg.baudRate},
            {"address", cfg.modbusAddress},
            {"flow_rate", cfg.flowRate},
            {"flow_rate_unit", cfg.flowRateUnit},
            {"direction", cfg.direction == SyringePumpService::Direction::Infuse ? 0 : 1},
            {"syringe_volume", cfg.syringeVolume},
            {"syringe_volume_unit", cfg.syringeVolumeUnit},
        });
    }
    config["pump_ports"] = pumpArray;
    writeJsonConfig(config);
}

QString SyringePumpTab::configPath() const {
    QSettings s;
    const QString external = s.value("Config/ExternalAppConfigPath").toString().trimmed();
    if (!external.isEmpty()) {
        return external;
    }
    return QDir(getUserConfigDir()).absoluteFilePath("config.json");
}

void SyringePumpTab::onAddPump() {
    const auto nextIndex = pumpService_.pumpCount() + 1;
    pumpService_.addPump(QStringLiteral("Pump %1").arg(static_cast<int>(nextIndex)));
    rebuildPumpRows();
    saveConfig();
}

void SyringePumpTab::onRemoveLastPump() {
    const auto handles = pumpService_.pumpHandles();
    if (handles.size() <= 1) {
        return;
    }
    pumpService_.disconnect(handles.back());
    pumpService_.removePump(handles.back());
    rebuildPumpRows();
    saveConfig();
}

void SyringePumpTab::onUpdateStatus() {
    for (auto& row : pumpRows_) {
        const auto handle =
            static_cast<SyringePumpService::PumpHandle>(row->property("pumpHandle").toULongLong());
        if (pumpService_.isConnected(handle)) {
            pumpService_.pollStatus(handle);
        }
        updatePumpUI(row.get());
    }
}

} // namespace frontend

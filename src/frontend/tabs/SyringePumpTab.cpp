#include "frontend/tabs/SyringePumpTab.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#endif

#include "backend/services/SyringePumpService.h"
#include "frontend/widgets/PumpRowWidget.h"

using json = nlohmann::json;
using PumpHandle = backend::services::SyringePumpService::PumpHandle;

namespace frontend {

namespace {
    // Resolve a writable config directory that works across platforms. Prefers
    // the app's AppDataLocation (QSettings-friendly, per-app namespacing via
    // QApplication::applicationName()). Falls back to exeDir/../include for
    // parity with the legacy layout used by MIB Studio.
    QString getConfigDir() {
        const QString app = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (!app.isEmpty()) {
            QDir().mkpath(app);
            return app;
        }
        return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../include");
    }

    constexpr uint16_t UNIT_UL_MIN = 100;
    constexpr uint16_t UNIT_ML_MIN = 103;
}

SyringePumpTab::SyringePumpTab(backend::services::SyringePumpService& service,
                               QWidget* parent)
    : QWidget(parent), service_(service)
{
    auto* outer = new QVBoxLayout(this);

    // Top bar
    auto* top = new QHBoxLayout();
    addBtn_ = new QPushButton(tr("+ Add Pump"), this);
    countLabel_ = new QLabel(tr("Pumps: 0"), this);
    top->addWidget(addBtn_);
    top->addWidget(countLabel_);
    top->addStretch(1);
    outer->addLayout(top);

    // Scrollable pump list
    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    auto* inner = new QWidget(scroll_);
    pumpsLayout_ = new QVBoxLayout(inner);
    pumpsLayout_->setContentsMargins(0, 0, 0, 0);
    pumpsLayout_->addStretch(1);
    scroll_->setWidget(inner);
    outer->addWidget(scroll_, 1);

    connect(addBtn_, &QPushButton::clicked, this, &SyringePumpTab::onAddPumpClicked);

    // Poll timer
    statusTimer_ = new QTimer(this);
    statusTimer_->setInterval(500);
    connect(statusTimer_, &QTimer::timeout, this, &SyringePumpTab::onUpdateStatus);
    statusTimer_->start();

    // Save config when the app quits (captures live settings edited in rows).
    connect(qApp, &QApplication::aboutToQuit, this, [this]() { saveConfig(); });

    // Load persisted pumps and rebuild rows.
    loadConfig();
    syncRowsWithService();
}

SyringePumpTab::~SyringePumpTab() = default;

void SyringePumpTab::ensureDefaultPumps(const QStringList& defaultNames) {
    if (service_.pumpCount() > 0) return;
    for (const QString& n : defaultNames) {
        service_.addPump(n.toStdString());
    }
    syncRowsWithService();
    saveConfig();
}

QString SyringePumpTab::configPath() const {
    QSettings s;
    const QString external = s.value("Config/ExternalAppConfigPath").toString().trimmed();
    if (!external.isEmpty()) return external;
    return QDir(getConfigDir()).absoluteFilePath("config.json");
}

PumpRowWidget* SyringePumpTab::findRow(PumpHandle handle) const {
    for (auto* r : rows_) {
        if (r && r->handle() == handle) return r;
    }
    return nullptr;
}

void SyringePumpTab::addRowWidget(PumpHandle handle) {
    auto* row = new PumpRowWidget(service_, handle, this);
    connect(row, &PumpRowWidget::removeRequested,
            this, &SyringePumpTab::onRemovePump);
    connect(row, &PumpRowWidget::liveSettingsChanged,
            this, &SyringePumpTab::onLiveSettingsChanged);
    connect(row, &PumpRowWidget::nameChanged,
            this, &SyringePumpTab::onNameChanged);

    // Insert above the trailing stretch.
    const int insertAt = pumpsLayout_->count() - 1;
    pumpsLayout_->insertWidget(insertAt, row);
    rows_.push_back(row);

    countLabel_->setText(tr("Pumps: %1").arg(rows_.size()));
}

void SyringePumpTab::removeRowWidget(PumpHandle handle) {
    for (auto it = rows_.begin(); it != rows_.end(); ++it) {
        if (*it && (*it)->handle() == handle) {
            (*it)->deleteLater();
            rows_.erase(it);
            break;
        }
    }
    countLabel_->setText(tr("Pumps: %1").arg(rows_.size()));
}

void SyringePumpTab::syncRowsWithService() {
    const auto handles = service_.pumpHandles();

    // Remove rows whose handle is no longer in the service.
    std::vector<PumpHandle> toRemove;
    for (auto* r : rows_) {
        if (!r) continue;
        const auto h = r->handle();
        if (std::find(handles.begin(), handles.end(), h) == handles.end()) {
            toRemove.push_back(h);
        }
    }
    for (auto h : toRemove) removeRowWidget(h);

    // Add rows for handles that don't yet have a widget.
    for (auto h : handles) {
        if (!findRow(h)) addRowWidget(h);
    }
}

void SyringePumpTab::onAddPumpClicked() {
    const auto id = service_.addPump(std::string("Pump ") + std::to_string(service_.pumpCount() + 1));
    addRowWidget(id);
    saveConfig();
}

void SyringePumpTab::onRemovePump(PumpHandle handle) {
    service_.removePump(handle);
    removeRowWidget(handle);
    saveConfig();
}

void SyringePumpTab::onUpdateStatus() {
    for (auto* r : rows_) {
        if (r) r->refresh();
    }
}

void SyringePumpTab::onLiveSettingsChanged(PumpHandle /*handle*/) {
    saveConfig();
}

void SyringePumpTab::onNameChanged(PumpHandle /*handle*/, const QString& /*name*/) {
    saveConfig();
}

// ---------------------------------------------------------------------------
// Config persistence (pumps array + legacy fallback)
// ---------------------------------------------------------------------------
void SyringePumpTab::loadConfig() {
    const QString path = configPath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;  // no config yet; caller may seed defaults
    }

    json cfg;
    try {
        QByteArray data = file.readAll();
        if (data.isEmpty()) return;
        cfg = json::parse(data.constData(), data.constData() + data.size());
    } catch (const std::exception& e) {
        SPDLOG_ERROR("SyringePumpTab: Failed to parse config.json: {}", e.what());
        return;
    }

    // Prefer new schema.
    if (cfg.contains("pumps") && cfg["pumps"].is_array()) {
        for (const auto& item : cfg["pumps"]) {
            const std::string name = item.value("name", std::string("Pump"));
            const auto id = service_.addPump(name);

            backend::services::SyringePumpService::PumpConfig pc;
            pc.portName = QString::fromStdString(item.value("port_name", std::string()));
            pc.baudRate = item.value("baud_rate", 115200);
            pc.modbusAddress = static_cast<uint8_t>(item.value("address", 1));
            pc.syringeVolume = static_cast<uint16_t>(item.value("syringe_vol", 10));
            pc.syringeVolumeUnit = static_cast<uint16_t>(item.value("syringe_unit", 103));
            pc.flowRate = item.value("flow_rate", 1.0);
            pc.flowRateUnit = static_cast<uint16_t>(item.value("flow_unit", static_cast<int>(UNIT_UL_MIN)));
            pc.direction = item.value("direction", 0) == 1
                ? backend::services::SyringePumpService::Direction::Withdraw
                : backend::services::SyringePumpService::Direction::Infuse;
            service_.setConfig(id, pc);
        }
        return;
    }

    // Legacy schema: auto-create "Sample" + "Sheath" from flat keys.
    auto migrateOne = [&](const std::string& prefix, const std::string& friendly) {
        const auto id = service_.addPump(friendly);
        backend::services::SyringePumpService::PumpConfig pc;
        // Port: try new-style name key first, then legacy integer.
        if (cfg.contains(prefix + "_com_port_name")) {
            pc.portName = QString::fromStdString(cfg[prefix + "_com_port_name"].get<std::string>());
        } else if (cfg.contains(prefix + "_com_port")) {
#ifdef _WIN32
            pc.portName = QString("COM%1").arg(cfg[prefix + "_com_port"].get<int>());
#endif
        }
        pc.baudRate = cfg.value(prefix + "_baud_rate", 115200);
        pc.modbusAddress = static_cast<uint8_t>(cfg.value(prefix + "_address", 1));
        pc.syringeVolume = static_cast<uint16_t>(cfg.value(prefix + "_syringe_vol", 10));
        pc.syringeVolumeUnit = static_cast<uint16_t>(cfg.value(prefix + "_syringe_unit", 103));
        pc.flowRate = cfg.value(prefix + "_flow_rate", 1.0);
        pc.flowRateUnit = static_cast<uint16_t>(cfg.value(prefix + "_flow_unit", static_cast<int>(UNIT_UL_MIN)));
        pc.direction = cfg.value(prefix + "_direction", 0) == 1
            ? backend::services::SyringePumpService::Direction::Withdraw
            : backend::services::SyringePumpService::Direction::Infuse;
        service_.setConfig(id, pc);
    };
    if (cfg.contains("pump_sample_com_port") || cfg.contains("pump_sample_com_port_name")) {
        migrateOne("pump_sample", "Sample");
    }
    if (cfg.contains("pump_sheath_com_port") || cfg.contains("pump_sheath_com_port_name")) {
        migrateOne("pump_sheath", "Sheath");
    }
}

void SyringePumpTab::saveConfig() {
    const QString path = configPath();
    QFile file(path);
    if (!file.open(QIODevice::ReadWrite | QIODevice::Text)) {
        SPDLOG_WARN("SyringePumpTab: cannot open {} for writing", path.toStdString());
        return;
    }

    json cfg;
    try {
        QByteArray data = file.readAll();
        if (!data.isEmpty())
            cfg = json::parse(data.constData(), data.constData() + data.size());
    } catch (const std::exception&) {
        cfg = json::object();
    }

    json arr = json::array();
    for (const auto handle : service_.pumpHandles()) {
        auto* row = findRow(handle);
        const auto pc = service_.getConfig(handle);
        json item;
        item["name"] = service_.pumpName(handle);
        item["port_name"] = pc.portName.toStdString();
        item["baud_rate"] = pc.baudRate;
        item["address"] = static_cast<int>(pc.modbusAddress);
        item["syringe_vol"] = pc.syringeVolume;
        item["syringe_unit"] = pc.syringeVolumeUnit;
        if (row) {
            item["flow_rate"] = row->flowRate();
            item["flow_unit"] = row->flowUnit();
            item["direction"] = row->directionIndex();
        } else {
            item["flow_rate"] = pc.flowRate;
            item["flow_unit"] = pc.flowRateUnit;
            item["direction"] = pc.direction == backend::services::SyringePumpService::Direction::Withdraw ? 1 : 0;
        }
        arr.push_back(item);
    }
    cfg["pumps"] = arr;

    // Drop legacy flat keys so the next load uses only the new schema.
    for (const char* prefix : {"pump_sample", "pump_sheath"}) {
        for (const char* suffix : {"_com_port", "_com_port_name", "_baud_rate", "_address",
                                   "_syringe_vol", "_syringe_unit", "_flow_rate",
                                   "_flow_unit", "_direction"}) {
            cfg.erase(std::string(prefix) + suffix);
        }
    }

    try {
        file.resize(0);
        QTextStream out(&file);
        out << QString::fromStdString(cfg.dump(4));
    } catch (const std::exception& e) {
        SPDLOG_ERROR("SyringePumpTab: Failed to save config.json: {}", e.what());
    }
}

} // namespace frontend

#include "frontend/PreviewPage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedLayout>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QSettings>

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include "backend/AppBackend.h"
#include "backend/services/AutofocusService.h"
#include "backend/services/CaptureService.h"
#include "frontend/PlaybackPanel.h"
#include "frontend/ConfigTabs.h"

using json = nlohmann::json;

namespace frontend {

PreviewPage::PreviewPage(backend::AppBackend& backend, QWidget* parent)
    : QWidget(parent), backend_(backend) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    // Top: Playback panel with centered Play/Stop overlay
    QWidget* overlayContainer = new QWidget(this);
    overlayContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* stacked = new QStackedLayout(overlayContainer);
    stacked->setStackingMode(QStackedLayout::StackAll);

    playback_ = new PlaybackPanel(backend_, overlayContainer);
    stacked->addWidget(playback_);

    QWidget* overlay = new QWidget(overlayContainer);
    auto* overlayLayout = new QHBoxLayout(overlay);
    overlayLayout->setAlignment(Qt::AlignCenter);

    playBtn_ = new QToolButton(overlay);
    playBtn_->setText(tr("▶ Play"));
    playBtn_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    playBtn_->setAutoRaise(true);
    playBtn_->setMinimumSize(120, 48);

    stopBtn_ = new QToolButton(overlay);
    stopBtn_->setText(tr("■ Stop"));
    stopBtn_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    stopBtn_->setAutoRaise(true);
    stopBtn_->setMinimumSize(120, 48);

    overlayLayout->addWidget(playBtn_);
    overlayLayout->addSpacing(12);
    overlayLayout->addWidget(stopBtn_);

    stacked->addWidget(overlay);

    connect(playBtn_, &QToolButton::clicked, this, &PreviewPage::onPlay);
    connect(stopBtn_, &QToolButton::clicked, this, &PreviewPage::onStop);

    // Periodically update overlay visibility
    auto* timer = new QTimer(this);
    timer->setInterval(300);
    connect(timer, &QTimer::timeout, this, &PreviewPage::onUpdateOverlay);
    timer->start();

    // Bottom: configuration tabs
    configTabs_ = new ConfigTabs(backend_, this);

    // Nanopositioner autofocus widget
    setupNanopositionerWidget();

    root->addWidget(overlayContainer, 3);
    root->addWidget(configTabs_, 2);
    root->addWidget(nanopositionerGroup_, 0);

    // Load config and update UI
    loadConfig();
    updateNanopositionerUI();

    // Status update timer
    statusUpdateTimer_ = new QTimer(this);
    statusUpdateTimer_->setInterval(500);
    connect(statusUpdateTimer_, &QTimer::timeout, this, &PreviewPage::onUpdateAutofocusStatus);
    statusUpdateTimer_->start();

    // Set status callback for autofocus service
    backend_.autofocus().setStatusCallback([this](const std::string& message) {
        if (statusLabel_) {
            statusLabel_->setText(QString::fromStdString(message));
        }
    });

    onUpdateOverlay();
}

void PreviewPage::onPlay() {
    auto& cap = backend_.capture();
    if (!cap.isRunning()) {
        SPDLOG_INFO("PreviewPage: starting capture");
        cap.start();
    }
    onUpdateOverlay();
}

void PreviewPage::onStop() {
    auto& cap = backend_.capture();
    if (cap.isRunning()) {
        SPDLOG_INFO("PreviewPage: stopping capture");
        cap.stop();
    }
    onUpdateOverlay();
}

void PreviewPage::onUpdateOverlay() {
    const bool running = backend_.capture().isRunning();
    playBtn_->setEnabled(!running);
    stopBtn_->setEnabled(running);
}

void PreviewPage::setupNanopositionerWidget() {
    nanopositionerGroup_ = new QGroupBox(tr("Nanopositioner Autofocus"), this);
    auto* layout = new QGridLayout(nanopositionerGroup_);

    // Connection settings row
    layout->addWidget(new QLabel(tr("COM Port:")), 0, 0);
    comPortSpinBox_ = new QSpinBox(nanopositionerGroup_);
    comPortSpinBox_->setRange(1, 256);
    comPortSpinBox_->setValue(6);
    layout->addWidget(comPortSpinBox_, 0, 1);

    layout->addWidget(new QLabel(tr("Baud Rate:")), 0, 2);
    baudRateCombo_ = new QComboBox(nanopositionerGroup_);
    baudRateCombo_->addItem("9600", 9600);
    baudRateCombo_->addItem("19200", 19200);
    baudRateCombo_->addItem("38400", 38400);
    baudRateCombo_->addItem("57600", 57600);
    baudRateCombo_->addItem("115200", 115200);
    baudRateCombo_->setCurrentIndex(4); // Default to 115200
    layout->addWidget(baudRateCombo_, 0, 3);

    layout->addWidget(new QLabel(tr("Device Address:")), 0, 4);
    deviceAddressSpinBox_ = new QSpinBox(nanopositionerGroup_);
    deviceAddressSpinBox_->setRange(0, 255);
    deviceAddressSpinBox_->setValue(1);
    layout->addWidget(deviceAddressSpinBox_, 0, 5);

    connectBtn_ = new QPushButton(tr("Connect"), nanopositionerGroup_);
    disconnectBtn_ = new QPushButton(tr("Disconnect"), nanopositionerGroup_);
    disconnectBtn_->setEnabled(false);
    layout->addWidget(connectBtn_, 0, 6);
    layout->addWidget(disconnectBtn_, 0, 7);

    connect(connectBtn_, &QPushButton::clicked, this, &PreviewPage::onConnectNanopositioner);
    connect(disconnectBtn_, &QPushButton::clicked, this, &PreviewPage::onDisconnectNanopositioner);

    // Autofocus control row
    autofocusEnabledCheck_ = new QCheckBox(tr("Enable Autofocus"), nanopositionerGroup_);
    autofocusEnabledCheck_->setEnabled(false);
    layout->addWidget(autofocusEnabledCheck_, 1, 0, 1, 2);
    connect(autofocusEnabledCheck_, &QCheckBox::stateChanged, this, &PreviewPage::onAutofocusEnabledChanged);

    statusLabel_ = new QLabel(tr("Not connected"), nanopositionerGroup_);
    layout->addWidget(statusLabel_, 1, 2, 1, 2);

    voltageLabel_ = new QLabel(tr("Voltage: -- V"), nanopositionerGroup_);
    layout->addWidget(voltageLabel_, 1, 4, 1, 2);

    // Manual voltage control row
    layout->addWidget(new QLabel(tr("Manual Control:")), 2, 0);
    increaseVoltageBtn_ = new QPushButton(tr("+"), nanopositionerGroup_);
    increaseVoltageBtn_->setEnabled(false);
    increaseVoltageBtn_->setMaximumWidth(40);
    layout->addWidget(increaseVoltageBtn_, 2, 1);

    decreaseVoltageBtn_ = new QPushButton(tr("-"), nanopositionerGroup_);
    decreaseVoltageBtn_->setEnabled(false);
    decreaseVoltageBtn_->setMaximumWidth(40);
    layout->addWidget(decreaseVoltageBtn_, 2, 2);

    layout->addWidget(new QLabel(tr("Step (V):")), 2, 3);
    voltageStepSpinBox_ = new QSpinBox(nanopositionerGroup_);
    voltageStepSpinBox_->setRange(1, 100);
    voltageStepSpinBox_->setValue(1);
    voltageStepSpinBox_->setSuffix(" V");
    voltageStepSpinBox_->setEnabled(false);
    layout->addWidget(voltageStepSpinBox_, 2, 4);

    connect(increaseVoltageBtn_, &QPushButton::clicked, this, &PreviewPage::onIncreaseVoltage);
    connect(decreaseVoltageBtn_, &QPushButton::clicked, this, &PreviewPage::onDecreaseVoltage);
}

void PreviewPage::updateNanopositionerUI() {
    auto& autofocus = backend_.autofocus();
    bool connected = autofocus.isConnected();
    bool enabled = autofocus.isEnabled();

    connectBtn_->setEnabled(!connected);
    disconnectBtn_->setEnabled(connected);
    comPortSpinBox_->setEnabled(!connected);
    baudRateCombo_->setEnabled(!connected);
    deviceAddressSpinBox_->setEnabled(!connected);
    autofocusEnabledCheck_->setEnabled(connected);
    autofocusEnabledCheck_->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
    increaseVoltageBtn_->setEnabled(connected);
    decreaseVoltageBtn_->setEnabled(connected);
    voltageStepSpinBox_->setEnabled(connected);

    if (connected) {
        double voltage = autofocus.getCurrentVoltage();
        voltageLabel_->setText(QString("Voltage: %1 V").arg(voltage, 0, 'f', 2));
    } else {
        voltageLabel_->setText("Voltage: -- V");
    }
}

void PreviewPage::onConnectNanopositioner() {
    int comPort = comPortSpinBox_->value();
    int baudRate = baudRateCombo_->currentData().toInt();
    unsigned char deviceAddress = static_cast<unsigned char>(deviceAddressSpinBox_->value());

    // Load config and apply to autofocus service
    loadConfig();

    bool success = backend_.autofocus().connect(comPort, baudRate, deviceAddress);
    if (success) {
        saveConfig();
        updateNanopositionerUI();
    } else {
        QMessageBox::warning(this, tr("Connection Failed"), 
                            tr("Failed to connect to nanopositioner on COM%1").arg(comPort));
    }
}

void PreviewPage::onDisconnectNanopositioner() {
    backend_.autofocus().disconnect();
    updateNanopositionerUI();
}

void PreviewPage::onAutofocusEnabledChanged(int state) {
    backend_.autofocus().setEnabled(state == Qt::Checked);
}

void PreviewPage::onIncreaseVoltage() {
    // Update manual voltage step from UI
    backend::services::AutofocusService::Config config = backend_.autofocus().getConfig();
    config.manualVoltageStep = voltageStepSpinBox_->value();
    backend_.autofocus().setConfig(config);
    backend_.autofocus().increaseVoltage();
}

void PreviewPage::onDecreaseVoltage() {
    // Update manual voltage step from UI
    backend::services::AutofocusService::Config config = backend_.autofocus().getConfig();
    config.manualVoltageStep = voltageStepSpinBox_->value();
    backend_.autofocus().setConfig(config);
    backend_.autofocus().decreaseVoltage();
}

void PreviewPage::onUpdateAutofocusStatus() {
    updateNanopositionerUI();
}

QString PreviewPage::configPath() const {
    QSettings s;
    const QString external = s.value("Config/ExternalAppConfigPath").toString().trimmed();
    if (!external.isEmpty()) {
        return external;
    }
    const QString appDir = QCoreApplication::applicationDirPath();
    return QDir(appDir).absoluteFilePath("../include/config.json");
}

void PreviewPage::loadConfig() {
    QString path = configPath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        SPDLOG_WARN("Failed to load config.json from {}", path.toStdString());
        return;
    }

    try {
        QByteArray data = file.readAll();
        json config = json::parse(data.constData(), data.constData() + data.size());

        // Load autofocus settings
        if (config.contains("autofocus_com_port")) {
            comPortSpinBox_->setValue(config["autofocus_com_port"].get<int>());
        }
        if (config.contains("autofocus_baud_rate")) {
            int baudRate = config["autofocus_baud_rate"].get<int>();
            int index = baudRateCombo_->findData(baudRate);
            if (index >= 0) {
                baudRateCombo_->setCurrentIndex(index);
            }
        }
        if (config.contains("autofocus_device_address")) {
            deviceAddressSpinBox_->setValue(config["autofocus_device_address"].get<int>());
        }

        // Load autofocus service config
        backend::services::AutofocusService::Config afConfig;
        if (config.contains("autofocus_focus_setpoint")) {
            afConfig.focusSetpoint = config["autofocus_focus_setpoint"].get<double>();
        }
        if (config.contains("autofocus_focus_range")) {
            afConfig.focusRange = config["autofocus_focus_range"].get<double>();
        }
        if (config.contains("autofocus_voltage_step")) {
            afConfig.voltageStep = config["autofocus_voltage_step"].get<double>();
        }
        if (config.contains("autofocus_fine_voltage_step")) {
            afConfig.fineVoltageStep = config["autofocus_fine_voltage_step"].get<double>();
        }
        if (config.contains("autofocus_max_voltage")) {
            afConfig.maxVoltage = config["autofocus_max_voltage"].get<double>();
        }
        if (config.contains("autofocus_min_voltage")) {
            afConfig.minVoltage = config["autofocus_min_voltage"].get<double>();
        }
        if (config.contains("autofocus_initial_voltage")) {
            afConfig.initialVoltage = config["autofocus_initial_voltage"].get<double>();
        }
        if (config.contains("autofocus_manual_voltage_step")) {
            afConfig.manualVoltageStep = config["autofocus_manual_voltage_step"].get<double>();
            voltageStepSpinBox_->setValue(static_cast<int>(afConfig.manualVoltageStep));
        }
        if (config.contains("ring_ratio_stale_ms")) {
            afConfig.ringRatioStaleMs = config["ring_ratio_stale_ms"].get<int>();
        }
        if (config.contains("require_new_sample_per_step")) {
            afConfig.requireNewSamplePerStep = config["require_new_sample_per_step"].get<bool>();
        }
        if (config.contains("autofocus_min_samples_per_step")) {
            afConfig.minSamplesPerStep = config["autofocus_min_samples_per_step"].get<int>();
        }
        if (config.contains("safe_shutdown_voltage")) {
            afConfig.safeShutdownVoltage = config["safe_shutdown_voltage"].get<double>();
        }
        if (config.contains("focus_direction")) {
            afConfig.focusDirection = config["focus_direction"].get<bool>();
        }

        backend_.autofocus().setConfig(afConfig);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Failed to parse config.json: {}", e.what());
    }
}

void PreviewPage::saveConfig() {
    QString path = configPath();
    QFile file(path);
    if (!file.open(QIODevice::ReadWrite | QIODevice::Text)) {
        SPDLOG_WARN("Failed to save config.json to {}", path.toStdString());
        return;
    }

    try {
        QByteArray data = file.readAll();
        json config = json::parse(data.constData(), data.constData() + data.size());

        // Save autofocus settings
        config["autofocus_com_port"] = comPortSpinBox_->value();
        config["autofocus_baud_rate"] = baudRateCombo_->currentData().toInt();
        config["autofocus_device_address"] = deviceAddressSpinBox_->value();

        file.resize(0);
        QTextStream out(&file);
        out << QString::fromStdString(config.dump(4));
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Failed to save config.json: {}", e.what());
    }
}

} // namespace frontend




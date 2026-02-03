#include "frontend/tabs/NanopositionerTab.h"
#include "ui_NanopositionerTab.h"

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
#define NOMINMAX // Prevent Windows.h from defining min/max macros
#include <windows.h>
#include <shlobj.h>
#endif

#include "backend/AppBackend.h"
#include "backend/Tools.h"
#include "backend/services/AutofocusService.h"

using json = nlohmann::json;

namespace frontend
{

	namespace
	{
		// Get user-writable config directory, falling back to ../include/ for development
		static QString getUserConfigDir()
		{
			QString appDir = QCoreApplication::applicationDirPath();
			QString appDirLower = appDir.toLower();

#ifdef _WIN32
			// Check if installed in Program Files (requires admin to write)
			if (appDirLower.contains("program files") ||
				appDirLower.contains("program files (x86)"))
			{
				// Use user-writable location
				char appDataPath[MAX_PATH];
				if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appDataPath)))
				{
					QString userConfigDir = QDir(QString::fromStdString(std::string(appDataPath) + "\\MIB_Studio_Qt\\include")).absolutePath();
					// Ensure directory exists
					QDir().mkpath(userConfigDir);
					return userConfigDir;
				}
			}
#endif
			// Development: use ../include/ relative to executable
			return QDir(appDir).absoluteFilePath("../include");
		}

		// Ensure default config exists if path points to default app config location
		static void ensureDefaultConfigExists(const QString &path)
		{
			// Only ensure when path points to app include path
			const QString defaultPath = QDir(getUserConfigDir()).absoluteFilePath("config.json");
			if (QFileInfo(path).absoluteFilePath() != QFileInfo(defaultPath).absoluteFilePath())
			{
				return;
			}
			QFileInfo fi(path);
			QDir dir(fi.absolutePath());
			if (!dir.exists())
			{
				dir.mkpath(".");
			}
			if (!QFile::exists(path))
			{
				QFile res(":/defaults/config.json");
				if (res.open(QIODevice::ReadOnly))
				{
					QFile out(path);
					if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
					{
						const QByteArray data = res.readAll();
						if (out.write(data) != data.size())
						{
							SPDLOG_WARN("NanopositionerTab: failed to write default config.json to {}", path.toStdString());
						}
					}
					else
					{
						SPDLOG_WARN("NanopositionerTab: failed to create {}", path.toStdString());
					}
				}
				else
				{
					SPDLOG_WARN("NanopositionerTab: failed to open resource defaults/config.json");
				}
			}
		}
	}

	NanopositionerTab::NanopositionerTab(backend::AppBackend &backend, QWidget *parent)
		: QWidget(parent), ui(new Ui::NanopositionerTab), backend_(backend)
	{
		ui->setupUi(this);

		// Configure baud rate combo with data values
		ui->baudRateCombo->setItemData(0, 9600);
		ui->baudRateCombo->setItemData(1, 19200);
		ui->baudRateCombo->setItemData(2, 38400);
		ui->baudRateCombo->setItemData(3, 57600);
		ui->baudRateCombo->setItemData(4, 115200);
		ui->baudRateCombo->setCurrentIndex(4); // Default to 115200

		// Connect signals
		connect(ui->connectBtn, &QPushButton::clicked, this, &NanopositionerTab::onConnectNanopositioner);
		connect(ui->disconnectBtn, &QPushButton::clicked, this, &NanopositionerTab::onDisconnectNanopositioner);
		connect(ui->refreshComPortBtn, &QPushButton::clicked, this, &NanopositionerTab::populateComPortList);
		connect(ui->autofocusEnabledCheck, &QCheckBox::stateChanged, this, &NanopositionerTab::onAutofocusEnabledChanged);
		connect(ui->increaseVoltageBtn, &QPushButton::clicked, this, &NanopositionerTab::onIncreaseVoltage);
		connect(ui->decreaseVoltageBtn, &QPushButton::clicked, this, &NanopositionerTab::onDecreaseVoltage);

		// Load config first so probe/auto-connect use saved baud and device address
		loadConfig();
		// Populate COM port list (probe uses baud/address from config now)
		populateComPortList();
		updateNanopositionerUI();

		// Status update timer
		statusUpdateTimer_ = new QTimer(this);
		statusUpdateTimer_->setInterval(500);
		connect(statusUpdateTimer_, &QTimer::timeout, this, &NanopositionerTab::onUpdateAutofocusStatus);
		statusUpdateTimer_->start();

		// Set status callback for autofocus service
		backend_.autofocus().setStatusCallback([this](const std::string &message)
											   {
		if (ui->statusLabel) {
			ui->statusLabel->setText(QString::fromStdString(message));
		} });

		// Delay auto-connect so COM/USB has time to enumerate (singleShot(0) is often too early)
		QTimer::singleShot(1800, this, &NanopositionerTab::tryAutoConnectNanopositioner);
	}

	NanopositionerTab::~NanopositionerTab() {
		delete ui;
	}

	void NanopositionerTab::populateComPortList()
	{
		ui->comPortCombo->clear();
		if (backend_.autofocus().isConnected())
		{
			// Do not probe while connected (SDK uses single global COM handle). Show current port only.
			int port = backend_.autofocus().getComPort();
			ui->comPortCombo->addItem(QString("COM%1").arg(port), port);
			ui->comPortCombo->setCurrentIndex(0);
			return;
		}
		int baudRate = ui->baudRateCombo->currentData().toInt();
		unsigned char deviceAddress = static_cast<unsigned char>(ui->deviceAddressSpinBox->value());
		std::vector<int> ports = backend::Tools::availableComPortNumbers();
		for (int port : ports)
		{
			if (backend::services::AutofocusService::probeComPort(port, baudRate, deviceAddress))
			{
				ui->comPortCombo->addItem(QString("COM%1").arg(port), port);
			}
		}
	}

	void NanopositionerTab::tryAutoConnectNanopositioner()
	{
		if (backend_.autofocus().isConnected())
		{
			return;
		}
		populateComPortList();
		// If no nanopositioner found and we haven't retried yet, try once more after a delay (USB can enumerate late).
		if (ui->comPortCombo->count() == 0 && !autoConnectRetried_)
		{
			autoConnectRetried_ = true;
			QTimer::singleShot(2500, this, &NanopositionerTab::tryAutoConnectNanopositioner);
			return;
		}
		// Auto-connect only when exactly one port responded as nanopositioner (probe passed).
		if (ui->comPortCombo->count() != 1)
		{
			return;
		}
		const int port = ui->comPortCombo->currentData().toInt();
		ui->comPortCombo->setCurrentIndex(0);
		loadConfig();
		int baudRate = ui->baudRateCombo->currentData().toInt();
		unsigned char deviceAddress = static_cast<unsigned char>(ui->deviceAddressSpinBox->value());
		bool success = backend_.autofocus().connect(port, baudRate, deviceAddress);
		if (success)
		{
			saveConfig();
			updateNanopositionerUI();
			SPDLOG_INFO("NanopositionerTab: auto-connected to nanopositioner on COM{}", port);
		}
		else
		{
			if (ui->statusLabel)
			{
				ui->statusLabel->setText(tr("Auto-connect failed on COM%1").arg(port));
			}
		}
	}

	void NanopositionerTab::updateNanopositionerUI()
	{
		auto &autofocus = backend_.autofocus();
		bool connected = autofocus.isConnected();
		bool enabled = autofocus.isEnabled();

		ui->connectBtn->setEnabled(!connected && ui->comPortCombo->count() > 0);
		ui->disconnectBtn->setEnabled(connected);
		ui->comPortCombo->setEnabled(!connected);
		ui->refreshComPortBtn->setEnabled(!connected);
		ui->baudRateCombo->setEnabled(!connected);
		ui->deviceAddressSpinBox->setEnabled(!connected);
		ui->autofocusEnabledCheck->setEnabled(connected);
		ui->autofocusEnabledCheck->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
		ui->increaseVoltageBtn->setEnabled(connected);
		ui->decreaseVoltageBtn->setEnabled(connected);
		ui->voltageStepSpinBox->setEnabled(connected);

		if (connected)
		{
			double voltage = autofocus.getCurrentVoltage();
			ui->voltageLabel->setText(QString("Voltage: %1 V").arg(voltage, 0, 'f', 2));
		}
		else
		{
			ui->voltageLabel->setText("Voltage: -- V");
		}
	}

	void NanopositionerTab::onConnectNanopositioner()
	{
		if (ui->comPortCombo->count() == 0)
		{
			return;
		}
		int comPort = ui->comPortCombo->currentData().toInt();
		int baudRate = ui->baudRateCombo->currentData().toInt();
		unsigned char deviceAddress = static_cast<unsigned char>(ui->deviceAddressSpinBox->value());

		// Load config and apply to autofocus service
		loadConfig();

		bool success = backend_.autofocus().connect(comPort, baudRate, deviceAddress);
		if (success)
		{
			saveConfig();
			updateNanopositionerUI();
		}
		else
		{
			QMessageBox::warning(this, tr("Connection Failed"),
								 tr("Failed to connect to nanopositioner on COM%1").arg(comPort));
		}
	}

	void NanopositionerTab::onDisconnectNanopositioner()
	{
		backend_.autofocus().disconnect();
		updateNanopositionerUI();
	}

	void NanopositionerTab::onAutofocusEnabledChanged(int state)
	{
		backend_.autofocus().setEnabled(state == Qt::Checked);
	}

	void NanopositionerTab::onIncreaseVoltage()
	{
		// Update manual voltage step from UI
		backend::services::AutofocusService::Config config = backend_.autofocus().getConfig();
		config.manualVoltageStep = ui->voltageStepSpinBox->value();
		backend_.autofocus().setConfig(config);
		backend_.autofocus().increaseVoltage();
	}

	void NanopositionerTab::onDecreaseVoltage()
	{
		// Update manual voltage step from UI
		backend::services::AutofocusService::Config config = backend_.autofocus().getConfig();
		config.manualVoltageStep = ui->voltageStepSpinBox->value();
		backend_.autofocus().setConfig(config);
		backend_.autofocus().decreaseVoltage();
	}

	void NanopositionerTab::onUpdateAutofocusStatus()
	{
		updateNanopositionerUI();
	}

	QString NanopositionerTab::configPath() const
	{
		QSettings s;
		const QString external = s.value("Config/ExternalAppConfigPath").toString().trimmed();
		if (!external.isEmpty())
		{
			return external;
		}
		// Use centralized helper to get user-writable config directory
		return QDir(getUserConfigDir()).absoluteFilePath("config.json");
	}

	void NanopositionerTab::loadConfig()
	{
		QString path = configPath();
		ensureDefaultConfigExists(path);
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		{
			SPDLOG_WARN("Failed to load config.json from {}", path.toStdString());
			return;
		}

		try
		{
			QByteArray data = file.readAll();
			json config = json::parse(data.constData(), data.constData() + data.size());

			// Load autofocus settings
			if (config.contains("autofocus_com_port"))
			{
				int port = config["autofocus_com_port"].get<int>();
				int idx = ui->comPortCombo->findData(port);
				if (idx >= 0)
				{
					ui->comPortCombo->setCurrentIndex(idx);
				}
			}
			if (config.contains("autofocus_baud_rate"))
			{
				int baudRate = config["autofocus_baud_rate"].get<int>();
				int index = ui->baudRateCombo->findData(baudRate);
				if (index >= 0)
				{
					ui->baudRateCombo->setCurrentIndex(index);
				}
			}
			if (config.contains("autofocus_device_address"))
			{
				ui->deviceAddressSpinBox->setValue(config["autofocus_device_address"].get<int>());
			}

			// Load autofocus service config
			backend::services::AutofocusService::Config afConfig;
			if (config.contains("autofocus_focus_setpoint"))
			{
				afConfig.focusSetpoint = config["autofocus_focus_setpoint"].get<double>();
			}
			if (config.contains("autofocus_focus_range"))
			{
				afConfig.focusRange = config["autofocus_focus_range"].get<double>();
			}
			if (config.contains("autofocus_voltage_step"))
			{
				afConfig.voltageStep = config["autofocus_voltage_step"].get<double>();
			}
			if (config.contains("autofocus_fine_voltage_step"))
			{
				afConfig.fineVoltageStep = config["autofocus_fine_voltage_step"].get<double>();
			}
			if (config.contains("autofocus_max_voltage"))
			{
				afConfig.maxVoltage = config["autofocus_max_voltage"].get<double>();
			}
			if (config.contains("autofocus_min_voltage"))
			{
				afConfig.minVoltage = config["autofocus_min_voltage"].get<double>();
			}
			if (config.contains("autofocus_initial_voltage"))
			{
				afConfig.initialVoltage = config["autofocus_initial_voltage"].get<double>();
			}
			if (config.contains("autofocus_manual_voltage_step"))
			{
				afConfig.manualVoltageStep = config["autofocus_manual_voltage_step"].get<double>();
				ui->voltageStepSpinBox->setValue(static_cast<int>(afConfig.manualVoltageStep));
			}
			if (config.contains("ring_ratio_stale_ms"))
			{
				afConfig.ringRatioStaleMs = config["ring_ratio_stale_ms"].get<int>();
			}
			if (config.contains("require_new_sample_per_step"))
			{
				afConfig.requireNewSamplePerStep = config["require_new_sample_per_step"].get<bool>();
			}
			if (config.contains("autofocus_min_samples_per_step"))
			{
				afConfig.minSamplesPerStep = config["autofocus_min_samples_per_step"].get<int>();
			}
			if (config.contains("safe_shutdown_voltage"))
			{
				afConfig.safeShutdownVoltage = config["safe_shutdown_voltage"].get<double>();
			}
			if (config.contains("focus_direction"))
			{
				afConfig.focusDirection = config["focus_direction"].get<bool>();
			}

			backend_.autofocus().setConfig(afConfig);
		}
		catch (const std::exception &e)
		{
			SPDLOG_ERROR("Failed to parse config.json: {}", e.what());
		}
	}

	void NanopositionerTab::saveConfig()
	{
		QString path = configPath();
		QFile file(path);
		if (!file.open(QIODevice::ReadWrite | QIODevice::Text))
		{
			SPDLOG_WARN("Failed to save config.json to {}", path.toStdString());
			return;
		}

		try
		{
			QByteArray data = file.readAll();
			json config = json::parse(data.constData(), data.constData() + data.size());

			// Save autofocus settings
			if (ui->comPortCombo->currentIndex() >= 0)
			{
				config["autofocus_com_port"] = ui->comPortCombo->currentData().toInt();
			}
			config["autofocus_baud_rate"] = ui->baudRateCombo->currentData().toInt();
			config["autofocus_device_address"] = ui->deviceAddressSpinBox->value();

			file.resize(0);
			QTextStream out(&file);
			out << QString::fromStdString(config.dump(4));
		}
		catch (const std::exception &e)
		{
			SPDLOG_ERROR("Failed to save config.json: {}", e.what());
		}
	}

} // namespace frontend

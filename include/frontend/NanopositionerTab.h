#pragma once

#include <QWidget>

namespace backend { class AppBackend; }
class QGroupBox;
class QSpinBox;
class QComboBox;
class QPushButton;
class QCheckBox;
class QLabel;
class QTimer;

namespace frontend {

class NanopositionerTab : public QWidget {
	Q_OBJECT
public:
	explicit NanopositionerTab(backend::AppBackend& backend, QWidget* parent = nullptr);

private slots:
	void onConnectNanopositioner();
	void onDisconnectNanopositioner();
	void onAutofocusEnabledChanged(int state);
	void onIncreaseVoltage();
	void onDecreaseVoltage();
	void onUpdateAutofocusStatus();

private:
	void setupUi();
	void updateNanopositionerUI();
	void loadConfig();
	void saveConfig();
	QString configPath() const;

	backend::AppBackend& backend_;

	// Nanopositioner autofocus UI
	QGroupBox* nanopositionerGroup_ = nullptr;
	QSpinBox* comPortSpinBox_ = nullptr;
	QComboBox* baudRateCombo_ = nullptr;
	QSpinBox* deviceAddressSpinBox_ = nullptr;
	QPushButton* connectBtn_ = nullptr;
	QPushButton* disconnectBtn_ = nullptr;
	QCheckBox* autofocusEnabledCheck_ = nullptr;
	QLabel* statusLabel_ = nullptr;
	QLabel* voltageLabel_ = nullptr;
	QPushButton* increaseVoltageBtn_ = nullptr;
	QPushButton* decreaseVoltageBtn_ = nullptr;
	QSpinBox* voltageStepSpinBox_ = nullptr;
	QTimer* statusUpdateTimer_ = nullptr;
};

} // namespace frontend






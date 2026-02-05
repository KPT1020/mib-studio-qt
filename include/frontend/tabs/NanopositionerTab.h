#pragma once

#include <QWidget>

namespace backend { class AppBackend; }
class QTimer;
namespace Ui { class NanopositionerTab; }

namespace frontend {

class NanopositionerTab : public QWidget {
	Q_OBJECT
public:
	explicit NanopositionerTab(backend::AppBackend& backend, QWidget* parent = nullptr);
	~NanopositionerTab();

	/** Used by DeviceInitManager before starting probe worker. */
	int getBaudRate() const;
	unsigned char getDeviceAddress() const;
	/** Called by DeviceInitManager on main thread to set status text (e.g. "Searching..."). */
	void setNanopositionerStatus(const QString& message);
	/** Called by DeviceInitManager on main thread after successful connect. Updates combo, saves config, refreshes UI. */
	void applyAutoConnectResult(int port);

private slots:
	void onConnectNanopositioner();
	void onDisconnectNanopositioner();
	void onAutofocusEnabledChanged(int state);
	void onIncreaseVoltage();
	void onDecreaseVoltage();
	void onUpdateAutofocusStatus();
	void onTargetRingWidthChanged(double value);

private:
	void updateNanopositionerUI();
	void loadConfig();
	void saveConfig();
	QString configPath() const;
	void populateComPortList();

	Ui::NanopositionerTab* ui;
	backend::AppBackend& backend_;
	QTimer* statusUpdateTimer_ = nullptr;
};

} // namespace frontend






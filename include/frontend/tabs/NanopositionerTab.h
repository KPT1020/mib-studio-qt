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

private slots:
	void onConnectNanopositioner();
	void onDisconnectNanopositioner();
	void onAutofocusEnabledChanged(int state);
	void onIncreaseVoltage();
	void onDecreaseVoltage();
	void onUpdateAutofocusStatus();

private:
	void updateNanopositionerUI();
	void loadConfig();
	void saveConfig();
	QString configPath() const;
	void populateComPortList();
	void tryAutoConnectNanopositioner();

	Ui::NanopositionerTab* ui;
	backend::AppBackend& backend_;
	QTimer* statusUpdateTimer_ = nullptr;
};

} // namespace frontend






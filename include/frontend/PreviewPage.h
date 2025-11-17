#pragma once

#include <QWidget>

namespace backend { class AppBackend; }
class QToolButton;
class PlaybackPanel; // global scope
class QGroupBox;
class QSpinBox;
class QComboBox;
class QPushButton;
class QCheckBox;
class QLabel;
class QTimer;

namespace frontend {

class ConfigTabs;

class PreviewPage : public QWidget {
    Q_OBJECT
public:
    explicit PreviewPage(backend::AppBackend& backend, QWidget* parent = nullptr);
    PlaybackPanel* getPlaybackPanel() const { return playback_; }

private slots:
    void onPlay();
    void onStop();
    void onUpdateOverlay();
    void onConnectNanopositioner();
    void onDisconnectNanopositioner();
    void onAutofocusEnabledChanged(int state);
    void onIncreaseVoltage();
    void onDecreaseVoltage();
    void onUpdateAutofocusStatus();

private:
    void setupNanopositionerWidget();
    void updateNanopositionerUI();
    void loadConfig();
    void saveConfig();
    QString configPath() const;

    backend::AppBackend& backend_;
    PlaybackPanel* playback_ = nullptr;
    ConfigTabs* configTabs_ = nullptr;
    QToolButton* playBtn_ = nullptr;
    QToolButton* stopBtn_ = nullptr;
    
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




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
namespace frontend { class AppConfigWatcher; }
namespace Ui { class PreviewPage; }

namespace frontend {

class ConfigTabs;

class PreviewPage : public QWidget {
    Q_OBJECT
public:
    explicit PreviewPage(backend::AppBackend& backend, QWidget* parent = nullptr);
    ~PreviewPage();
    PlaybackPanel* getPlaybackPanel() const { return playback_; }
    ConfigTabs* getConfigTabs() const { return configTabs_; }
    AppConfigWatcher* getConfigWatcher() const { return configWatcher_; }

    // Integrated Experiment dashboard (UX-7): MainWindow installs the run
    // header/metrics strip above the live image.
    void setDashboardWidget(QWidget* widget);

private slots:
    void onPlay();
    void onStop();
    void onUpdateOverlay();

private:
    void setupOverlayWidget();

    Ui::PreviewPage* ui;
    backend::AppBackend& backend_;
    PlaybackPanel* playback_ = nullptr;
    ConfigTabs* configTabs_ = nullptr;
    QToolButton* playBtn_ = nullptr;
    QToolButton* stopBtn_ = nullptr;
	AppConfigWatcher* configWatcher_ = nullptr;
};

} // namespace frontend




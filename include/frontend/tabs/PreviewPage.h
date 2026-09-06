#pragma once

#include <QWidget>

namespace backend { class AppBackend; }
class PlaybackPanel; // global scope
namespace frontend { class AppConfigWatcher; }
namespace Ui { class PreviewPage; }

namespace frontend {

class ConfigTabs;

// Preview workspace: live image (PlaybackPanel) above the configuration
// inspector (ConfigTabs). Carries no camera acquisition controls of its own
// (issue #360) — Start/Stop live in the application chrome and dispatch
// through MainWindow's CameraController.
class PreviewPage : public QWidget {
    Q_OBJECT
public:
    explicit PreviewPage(backend::AppBackend& backend, QWidget* parent = nullptr);
    ~PreviewPage();
    PlaybackPanel* getPlaybackPanel() const { return playback_; }
    ConfigTabs* getConfigTabs() const { return configTabs_; }
    AppConfigWatcher* getConfigWatcher() const { return configWatcher_; }

private:
    Ui::PreviewPage* ui;
    backend::AppBackend& backend_;
    PlaybackPanel* playback_ = nullptr;
    ConfigTabs* configTabs_ = nullptr;
    AppConfigWatcher* configWatcher_ = nullptr;
};

} // namespace frontend

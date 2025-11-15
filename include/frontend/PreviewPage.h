#pragma once

#include <QWidget>

namespace backend { class AppBackend; }
class QToolButton;
class PlaybackPanel; // global scope

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

private:
    backend::AppBackend& backend_;
    PlaybackPanel* playback_ = nullptr;
    ConfigTabs* configTabs_ = nullptr;
    QToolButton* playBtn_ = nullptr;
    QToolButton* stopBtn_ = nullptr;
};

} // namespace frontend




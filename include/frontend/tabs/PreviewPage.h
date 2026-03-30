#pragma once

#include <QWidget>

namespace backend { class AppBackend; }
class QToolButton;
class PlaybackPanel; // global scope
namespace Ui { class PreviewPage; }

namespace frontend {

class PreviewPage : public QWidget {
    Q_OBJECT
public:
    explicit PreviewPage(backend::AppBackend& backend, QWidget* parent = nullptr);
    ~PreviewPage();
    PlaybackPanel* getPlaybackPanel() const { return playback_; }

private slots:
    void onPlay();
    void onStop();
    void onUpdateOverlay();

private:
    void setupOverlayWidget();

    Ui::PreviewPage* ui;
    backend::AppBackend& backend_;
    PlaybackPanel* playback_ = nullptr;
    QToolButton* playBtn_ = nullptr;
    QToolButton* stopBtn_ = nullptr;
};

} // namespace frontend

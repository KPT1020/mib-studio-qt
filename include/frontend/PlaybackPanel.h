#pragma once

#include <QWidget>
#include <cstdint>
#include <memory>

namespace backend { class AppBackend; }

class QTimer;
class QSlider;
class QWidget;

class PlaybackPanel : public QWidget {
    Q_OBJECT
public:
    explicit PlaybackPanel(backend::AppBackend& backend, QWidget* parent = nullptr);
    ~PlaybackPanel() override;

private slots:
    void onTick();
    void onSliderPressed();
    void onSliderReleased();
    void onSliderValueChanged(int value);

private:
    backend::AppBackend& backend_;
    QTimer* timer_ = nullptr;
    QWidget* canvas_ = nullptr;
    QSlider* slider_ = nullptr;
    bool scrubbing_ = false;
    bool followLive_ = true;           // auto-follow latest when true
    bool prevCaptureRunning_ = false;  // detect start transitions
    uint64_t pinnedIndex_ = 0;         // selected index when reviewing
    QImage frameImage_;
};



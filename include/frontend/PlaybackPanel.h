#pragma once

#include <QWidget>
#include <QImage>
#include <QRect>
#include <QList>
#include <QPolygon>
#include <cstdint>
#include <memory>

namespace backend { class AppBackend; }

class QTimer;
class QSlider;
class QWidget;
class QToolButton;

class PlaybackPanel : public QWidget {
    Q_OBJECT
public:
    explicit PlaybackPanel(backend::AppBackend& backend, QWidget* parent = nullptr);
    ~PlaybackPanel() override;

    enum class OverlayMode { Off, Mask, Contours, Both };

private slots:
    void onTick();
    void onSliderPressed();
    void onSliderReleased();
    void onSliderValueChanged(int value);
    void onToggleOverlay();
    void onSetBackground();

private:
    void computeProcessedOverlay();
    void updateOverlayButtonUi();

    backend::AppBackend& backend_;
    QTimer* timer_ = nullptr;
    QWidget* canvas_ = nullptr;
    QSlider* slider_ = nullptr;
    QToolButton* overlayBtn_ = nullptr;
    QToolButton* setBgBtn_ = nullptr;
    bool scrubbing_ = false;
    bool followLive_ = true;           // auto-follow latest when true
    bool prevCaptureRunning_ = false;  // detect start transitions
    uint64_t pinnedIndex_ = 0;         // selected index when reviewing
    QImage frameImage_;
    // Overlay/ROI state
    OverlayMode overlayMode_ { OverlayMode::Off };
    QRect imageRoi_;
    bool roiActive_ = false;
    bool hasBackground_ = false;
    QImage backgroundGray_;            // stored as grayscale QImage
    QImage overlayImage_;              // RGBA overlay (mask)
    QList<QPolygon> overlayContours_;  // image-space contours for drawing
};



#include "frontend/PlaybackPanel.h"

#include <QPainter>
#include <QTimer>

#include "backend/AppBackend.h"
#include "backend/services/PlaybackService.h"
#include "backend/playback/FrameStore.h"

PlaybackPanel::PlaybackPanel(backend::AppBackend& backend, QWidget* parent)
    : QWidget(parent), backend_(backend) {
    timer_ = new QTimer(this);
    timer_->setInterval(33); // ~30 FPS
    connect(timer_, &QTimer::timeout, this, &PlaybackPanel::onTick);
    timer_->start();
}

PlaybackPanel::~PlaybackPanel() = default;

void PlaybackPanel::onTick() {
    backend::playback::Frame f;
    if (backend_.playback().fetchLatest(f)) {
        // Convert Mono8 to QImage; fallback to grayscale if unknown
        if (f.pixelFormat == 0x01080001 /* PFNC Mono8 */ || true) {
            const int w = static_cast<int>(f.width);
            const int h = static_cast<int>(f.height);
            const int pitch = static_cast<int>(f.linePitch == 0 ? f.width : f.linePitch);
            QImage img(f.data.data(), w, h, pitch, QImage::Format_Grayscale8);
            frameImage_ = img.copy(); // ensure ownership
        } else {
            frameImage_ = QImage();
        }
        update();
    }
}

void PlaybackPanel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (!frameImage_.isNull()) {
        // Fit to widget while preserving aspect ratio
        QImage scaled = frameImage_.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QPoint topLeft((width() - scaled.width()) / 2, (height() - scaled.height()) / 2);
        p.drawImage(topLeft, scaled);
    }
}



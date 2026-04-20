#include "frontend/utils/RoiDrawCanvas.h"

#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <algorithm>

namespace frontend {

RoiDrawCanvas::RoiDrawCanvas(QWidget* parent)
    : QWidget(parent) {
    setMinimumSize(360, 270);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
}

void RoiDrawCanvas::setImage(const QImage& img) {
    image_ = img;
    update();
}

void RoiDrawCanvas::setRoi(const QRect& imageRect) {
    roi_ = imageRect;
    update();
}

// ---------------------------------------------------------------------------
// Transform helpers
// ---------------------------------------------------------------------------

void RoiDrawCanvas::updateTransform() {
    if (image_.isNull()) {
        scale_   = 1.0;
        topLeft_ = {};
        return;
    }
    const double sw = static_cast<double>(width())  / image_.width();
    const double sh = static_cast<double>(height()) / image_.height();
    scale_ = std::min(sw, sh);

    const double drawW = image_.width()  * scale_;
    const double drawH = image_.height() * scale_;
    topLeft_ = QPointF((width() - drawW) / 2.0, (height() - drawH) / 2.0);
}

QRectF RoiDrawCanvas::imageToCanvas(const QRectF& r) const {
    return QRectF(
        r.left()  * scale_ + topLeft_.x(),
        r.top()   * scale_ + topLeft_.y(),
        r.width() * scale_,
        r.height()* scale_);
}

QRect RoiDrawCanvas::canvasToImage(const QRect& r) const {
    if (image_.isNull()) return {};
    const int x = static_cast<int>((r.left()  - topLeft_.x()) / scale_);
    const int y = static_cast<int>((r.top()   - topLeft_.y()) / scale_);
    const int w = static_cast<int>(r.width()  / scale_);
    const int h = static_cast<int>(r.height() / scale_);

    // Clamp to image bounds
    const int cx = std::clamp(x, 0, image_.width());
    const int cy = std::clamp(y, 0, image_.height());
    const int cw = std::clamp(w, 0, image_.width()  - cx);
    const int ch = std::clamp(h, 0, image_.height() - cy);
    return QRect(cx, cy, cw, ch);
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

void RoiDrawCanvas::paintEvent(QPaintEvent*) {
    updateTransform();

    QPainter p(this);
    p.fillRect(rect(), Qt::black);

    if (!image_.isNull()) {
        const QRectF dst(topLeft_,
                         QSizeF(image_.width() * scale_, image_.height() * scale_));
        p.drawImage(dst, image_);
    }

    if (dragging_) {
        // Live rubber-band: yellow semi-transparent
        const QRect band = QRect(dragStart_, dragCurrent_).normalized();
        p.fillRect(band, QColor(255, 220, 0, 60));
        p.setPen(QPen(QColor(255, 220, 0), 1));
        p.drawRect(band);
    } else if (!roi_.isNull()) {
        // Committed ROI: red semi-transparent
        const QRectF canvasRoi = imageToCanvas(QRectF(roi_));
        p.fillRect(canvasRoi, QColor(220, 0, 0, 60));
        p.setPen(QPen(QColor(220, 0, 0), 1));
        p.drawRect(canvasRoi);
    }
}

void RoiDrawCanvas::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        dragging_    = true;
        dragStart_   = event->pos();
        dragCurrent_ = event->pos();
        update();
    }
}

void RoiDrawCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (dragging_) {
        dragCurrent_ = event->pos();
        update();
    }
}

void RoiDrawCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && dragging_) {
        dragging_    = false;
        dragCurrent_ = event->pos();

        const QRect band = QRect(dragStart_, dragCurrent_).normalized();
        roi_ = canvasToImage(band);
        update();
        emit roiChanged(roi_);
    }
}

} // namespace frontend

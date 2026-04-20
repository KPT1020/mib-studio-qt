#pragma once

#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QWidget>

class QPaintEvent;
class QMouseEvent;

namespace frontend {

// Canvas widget that displays a QImage and lets the user drag a rectangle to
// define an ROI in image coordinates. Emits roiChanged() after each completed
// drag. Call setRoi() to pre-populate from stored experiment metadata.
class RoiDrawCanvas : public QWidget {
    Q_OBJECT
public:
    explicit RoiDrawCanvas(QWidget* parent = nullptr);

    void  setImage(const QImage& img);
    void  setRoi(const QRect& imageRect);
    QRect getRoi() const { return roi_; }

signals:
    void roiChanged(QRect imageRect);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    // Map image-space rect → canvas-space rect and vice versa.
    QRectF imageToCanvas(const QRectF& r) const;
    QRect  canvasToImage(const QRect& r)  const;

    // Recompute scale_ and topLeft_ from current widget and image sizes.
    void updateTransform();

    QImage  image_;
    QRect   roi_;           // image coordinates; null rect = none

    bool    dragging_  = false;
    QPoint  dragStart_;     // canvas coordinates
    QPoint  dragCurrent_;   // canvas coordinates

    // Fit-to-window transform state (recomputed each paintEvent)
    double  scale_   = 1.0;
    QPointF topLeft_;
};

} // namespace frontend

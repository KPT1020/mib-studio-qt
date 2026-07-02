#pragma once

#include <QWidget>
#include <QPointF>
#include <QImage>
#include "frontend/tabs/OverviewTab.h"  // Need full definition for OverviewTab::FitMode enum

class QPaintEvent;
class QMouseEvent;

namespace frontend
{

    class SimpleImageCanvas : public QWidget
    {
        Q_OBJECT
    public:
        explicit SimpleImageCanvas(QImage *image, OverviewTab::FitMode *fitMode,
                                   bool *roiVisible, QPointF *roiPos,
                                   int *roiWidth = nullptr, int *roiHeight = nullptr,
                                   QWidget *parent = nullptr);

    signals:
        void roiPositionChanged(QPointF imagePos);

    protected:
        void paintEvent(QPaintEvent *) override;
        void mousePressEvent(QMouseEvent *event) override;
        void mouseMoveEvent(QMouseEvent *event) override;
        void mouseReleaseEvent(QMouseEvent *event) override;

    private:
        QPointF canvasToImage(const QPointF &canvasPos) const;
        QPointF imageToCanvas(const QPointF &imagePos) const;

        QImage *image_ = nullptr;
        OverviewTab::FitMode *fitMode_ = nullptr;
        bool *roiVisible_ = nullptr;
        QPointF *roiPos_ = nullptr;
        int *roiWidth_ = nullptr;
        int *roiHeight_ = nullptr;

        // Transformation state
        double scale_ = 1.0;
        QPointF topLeft_;
        QSizeF drawSize_;

        // Dragging state
        bool dragging_ = false;
        QPointF dragStartCanvasPos_;
        QPointF dragStartRoiPos_;

        // Scaled image cache: only rescale when image content or draw size changes
        mutable QImage scaledImgCache_;
        mutable qint64 lastImgCacheKey_{-1};
        mutable QSize  lastDrawSize_;
    };

} // namespace frontend

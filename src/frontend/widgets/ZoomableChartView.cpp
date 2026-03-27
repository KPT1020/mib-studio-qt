#include "frontend/widgets/ZoomableChartView.h"

#include <QWheelEvent>
#include <QMouseEvent>
#include <QChart>
#include <QValueAxis>
#include <QAbstractAxis>
#include <algorithm>

namespace frontend {

ZoomableChartView::ZoomableChartView(QChart* chart, QWidget* parent)
    : QChartView(chart, parent)
{
    setRubberBand(QChartView::NoRubberBand);
}

void ZoomableChartView::setDefaultRange(QValueAxis* axis, double min, double max)
{
    for (auto& d : defaultRanges_) {
        if (d.axis == axis) {
            d.min = min;
            d.max = max;
            return;
        }
    }
    defaultRanges_.push_back({axis, min, max});
}

void ZoomableChartView::resetZoom()
{
    for (const auto& d : defaultRanges_) {
        if (d.axis)
            d.axis->setRange(d.min, d.max);
    }
    isUserZoomed_ = false;
    emit zoomReset();
}

void ZoomableChartView::wheelEvent(QWheelEvent* event)
{
    if (!chart())
        return;

    const double delta = event->angleDelta().y();
    if (qFuzzyIsNull(delta))
        return;

    // Zoom factor: ~10% per notch (120 units = 1 notch)
    const double factor = 1.0 + delta / 1200.0;

    // Map cursor position to chart value coordinates
    const QPointF cursorPos = event->position();
    QAbstractSeries* series = nullptr;
    if (!chart()->series().isEmpty())
        series = chart()->series().first();

    const QPointF chartVal = chart()->mapToValue(cursorPos, series);
    const QRectF plotArea = chart()->plotArea();

    // Determine which axes to zoom based on cursor position and modifier keys:
    //   Ctrl held  → X axis only
    //   Shift held → Y axis only
    //   Cursor left of plot area (Y axis region) → Y only
    //   Cursor below plot area (X axis region)   → X only
    //   Cursor inside plot area (no modifier)    → both axes
    bool zoomX = true;
    bool zoomY = true;

    if (event->modifiers() & Qt::ControlModifier) {
        zoomY = false;
    } else if (event->modifiers() & Qt::ShiftModifier) {
        zoomX = false;
    } else if (cursorPos.x() < plotArea.left()) {
        zoomX = false;   // hovering over Y axis labels
    } else if (cursorPos.y() > plotArea.bottom()) {
        zoomY = false;   // hovering over X axis labels
    }

    // Zoom selected axes around the cursor position
    for (auto* axis : chart()->axes()) {
        auto* valueAxis = qobject_cast<QValueAxis*>(axis);
        if (!valueAxis)
            continue;

        bool isHorizontal = (axis->alignment() == Qt::AlignBottom || axis->alignment() == Qt::AlignTop);
        if (isHorizontal && !zoomX)
            continue;
        if (!isHorizontal && !zoomY)
            continue;

        double oldMin = valueAxis->min();
        double oldMax = valueAxis->max();
        double cursorVal = isHorizontal ? chartVal.x() : chartVal.y();

        double newMin = cursorVal - (cursorVal - oldMin) / factor;
        double newMax = cursorVal + (oldMax - cursorVal) / factor;

        // Prevent axis inversion
        if (newMin < newMax)
            valueAxis->setRange(newMin, newMax);
    }

    isUserZoomed_ = true;
    event->accept();
}

void ZoomableChartView::mouseDoubleClickEvent(QMouseEvent* event)
{
    resetZoom();
    event->accept();
}

void ZoomableChartView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        isPanning_ = true;
        lastPanPoint_ = event->position();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    } else {
        QChartView::mousePressEvent(event);
    }
}

void ZoomableChartView::mouseMoveEvent(QMouseEvent* event)
{
    if (isPanning_) {
        QAbstractSeries* series = nullptr;
        if (!chart()->series().isEmpty())
            series = chart()->series().first();

        QPointF oldVal = chart()->mapToValue(lastPanPoint_, series);
        QPointF newVal = chart()->mapToValue(event->position(), series);
        QPointF delta = oldVal - newVal;

        for (auto* axis : chart()->axes()) {
            auto* valueAxis = qobject_cast<QValueAxis*>(axis);
            if (!valueAxis)
                continue;

            double shift;
            if (axis->alignment() == Qt::AlignBottom || axis->alignment() == Qt::AlignTop)
                shift = delta.x();
            else
                shift = delta.y();

            valueAxis->setRange(valueAxis->min() + shift, valueAxis->max() + shift);
        }

        lastPanPoint_ = event->position();
        isUserZoomed_ = true;
        event->accept();
    } else {
        QChartView::mouseMoveEvent(event);
    }
}

void ZoomableChartView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && isPanning_) {
        isPanning_ = false;
        setCursor(Qt::ArrowCursor);
        event->accept();
    } else {
        QChartView::mouseReleaseEvent(event);
    }
}

} // namespace frontend

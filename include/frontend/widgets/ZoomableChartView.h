#pragma once

#include <QChartView>
#include <QValueAxis>
#include <vector>

class QWheelEvent;
class QMouseEvent;

namespace frontend {

class ZoomableChartView : public QChartView {
    Q_OBJECT
public:
    explicit ZoomableChartView(QChart* chart, QWidget* parent = nullptr);

    bool isUserZoomed() const { return isUserZoomed_; }
    void resetZoom();
    void setDefaultRange(QValueAxis* axis, double min, double max);

signals:
    void zoomReset();

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    bool isUserZoomed_ = false;
    bool isPanning_ = false;
    QPointF lastPanPoint_;

    struct AxisDefault {
        QValueAxis* axis = nullptr;
        double min = 0.0;
        double max = 1.0;
    };
    std::vector<AxisDefault> defaultRanges_;
};

} // namespace frontend

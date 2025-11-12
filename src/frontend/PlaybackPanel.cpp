#include "frontend/PlaybackPanel.h"

#include <QPainter>
#include <QTimer>
#include <QSlider>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QMouseEvent>
#include <QMenu>
#include <QCursor>
#include <limits>
#include <algorithm>

#include "backend/AppBackend.h"
#include "backend/services/CaptureService.h"
#include "backend/services/PlaybackService.h"
#include "backend/playback/FrameStore.h"

#include <spdlog/spdlog.h>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace
{

    class ImageCanvas : public QWidget
    {
    public:
        explicit ImageCanvas(QImage *image,
                             QImage *overlay,
                             QRect *imageRoi,
                             QList<QPolygon> *contours,
                             QWidget *parent = nullptr)
            : QWidget(parent),
              image_(image),
              overlay_(overlay),
              imageRoi_(imageRoi),
              contours_(contours) {}

        std::function<void(const QRect &)> onRoiSelected;
        std::function<void()> onRequestBackground;

    protected:
        void paintEvent(QPaintEvent *) override
        {
            QPainter p(this);
            p.fillRect(rect(), Qt::black);
            if (!image_ || image_->isNull())
                return;

            const int imgW = image_->width();
            const int imgH = image_->height();
            const double scale = std::min(double(width()) / imgW, double(height()) / imgH);
            const QSizeF drawSize(imgW * scale, imgH * scale);
            const QPointF topLeft((width() - drawSize.width()) / 2.0, (height() - drawSize.height()) / 2.0);

            // Base image
            QImage scaled = image_->scaled(drawSize.toSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            p.drawImage(topLeft.toPoint(), scaled);

            // Overlay mask (RGBA) if present
            if (overlay_ && !overlay_->isNull())
            {
                QImage scaledOverlay = overlay_->scaled(drawSize.toSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                p.drawImage(topLeft.toPoint(), scaledOverlay);
            }

            // Contours
            if (contours_)
            {
                QPen pen(QColor(0, 255, 0));
                pen.setWidth(2);
                p.setPen(pen);
                for (const QPolygon &poly : *contours_)
                {
                    if (poly.isEmpty())
                        continue;
                    QPolygon scaledPoly;
                    scaledPoly.reserve(poly.size());
                    for (const QPoint &pt : poly)
                    {
                        QPointF q = QPointF(pt) * scale + topLeft;
                        scaledPoly << q.toPoint();
                    }
                    p.drawPolyline(scaledPoly);
                }
            }

            // Final ROI
            if (imageRoi_ && !imageRoi_->isNull() && imageRoi_->isValid())
            {
                QPen roiPen(QColor(255, 255, 0));
                roiPen.setWidth(1);
                roiPen.setStyle(Qt::DashLine);
                p.setPen(roiPen);
                QRectF r(imageRoi_->x() * scale + topLeft.x(),
                         imageRoi_->y() * scale + topLeft.y(),
                         imageRoi_->width() * scale,
                         imageRoi_->height() * scale);
                p.drawRect(r);
            }

            // Rubber-band during drag
            if (dragging_)
            {
                QPen rbPen(QColor(0, 180, 255));
                rbPen.setWidth(1);
                rbPen.setStyle(Qt::DashDotLine);
                p.setPen(rbPen);
                QRect r = QRect(dragStartWidgetPos_, dragCurrentWidgetPos_).normalized();
                p.drawRect(r);
            }
        }

        void mousePressEvent(QMouseEvent *e) override
        {
            if (!image_ || image_->isNull())
                return;
            if (e->button() == Qt::LeftButton)
            {
                dragging_ = true;
                dragStartWidgetPos_ = e->pos();
                dragCurrentWidgetPos_ = e->pos();
                update();
            }
        }

        void mouseMoveEvent(QMouseEvent *e) override
        {
            if (!dragging_)
                return;
            dragCurrentWidgetPos_ = e->pos();
            update();
        }

        void mouseReleaseEvent(QMouseEvent *e) override
        {
            if (!image_ || image_->isNull())
                return;
            if (e->button() == Qt::LeftButton && dragging_)
            {
                dragging_ = false;
                dragCurrentWidgetPos_ = e->pos();
                // Map widget rect to image coordinates
                QRect widgetRect = QRect(dragStartWidgetPos_, dragCurrentWidgetPos_).normalized();
                if (widgetRect.width() < 3 || widgetRect.height() < 3)
                {
                    // Too small, ignore
                    update();
                    return;
                }
                const int imgW = image_->width();
                const int imgH = image_->height();
                const double scale = std::min(double(width()) / imgW, double(height()) / imgH);
                const QSizeF drawSize(imgW * scale, imgH * scale);
                const QPointF topLeft((width() - drawSize.width()) / 2.0, (height() - drawSize.height()) / 2.0);

                auto toImage = [&](const QPoint &wp) -> QPoint
                {
                    QPointF f = (wp - topLeft) / scale;
                    int x = std::clamp(int(std::lround(f.x())), 0, imgW - 1);
                    int y = std::clamp(int(std::lround(f.y())), 0, imgH - 1);
                    return QPoint(x, y);
                };

                QPoint imgP0 = toImage(widgetRect.topLeft());
                QPoint imgP1 = toImage(widgetRect.bottomRight());
                QRect imgRect = QRect(imgP0, imgP1).normalized();
                if (imageRoi_)
                    *imageRoi_ = imgRect;
                if (onRoiSelected)
                    onRoiSelected(imgRect);
                update();
            }
        }

        void contextMenuEvent(QContextMenuEvent *event) override
        {
            QMenu menu(this);
            QAction *setBg = menu.addAction("Set Background");
            QAction *clearRoi = menu.addAction("Clear ROI");
            QAction *chosen = menu.exec(event->globalPos());
            if (!chosen)
                return;
            if (chosen == setBg)
            {
                if (onRequestBackground)
                    onRequestBackground();
            }
            else if (chosen == clearRoi)
            {
                if (imageRoi_)
                    *imageRoi_ = QRect();
                if (onRoiSelected)
                    onRoiSelected(QRect());
                update();
            }
        }

    private:
        QImage *image_ = nullptr;
        QImage *overlay_ = nullptr;
        QRect *imageRoi_ = nullptr;
        QList<QPolygon> *contours_ = nullptr;
        bool dragging_ = false;
        QPoint dragStartWidgetPos_;
        QPoint dragCurrentWidgetPos_;
    };

} // namespace

PlaybackPanel::PlaybackPanel(backend::AppBackend &backend, QWidget *parent)
    : QWidget(parent), backend_(backend)
{
    // UI: image canvas + horizontal slider
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    canvas_ = new ImageCanvas(&frameImage_, &overlayImage_, &imageRoi_, &overlayContours_, this);
    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setRange(0, 0);
    slider_->setSingleStep(1);
    slider_->setPageStep(8);

    layout->addWidget(canvas_, 1);

    // Controls bar
    QWidget *controls = new QWidget(this);
    auto *controlsLayout = new QHBoxLayout(controls);
    controlsLayout->setContentsMargins(6, 4, 6, 4);
    controlsLayout->setSpacing(6);
    overlayBtn_ = new QToolButton(controls);
    overlayBtn_->setText("Overlay: Off");
    overlayBtn_->setToolTip("Toggle overlay (Off → Mask → Contours → Both)");
    setBgBtn_ = new QToolButton(controls);
    setBgBtn_->setText("Set Background");
    setBgBtn_->setToolTip("Capture current frame as background (when paused)");
    controlsLayout->addWidget(overlayBtn_);
    controlsLayout->addWidget(setBgBtn_);
    controlsLayout->addStretch(1);
    layout->addWidget(controls);

    layout->addWidget(slider_);

    connect(slider_, &QSlider::sliderPressed, this, &PlaybackPanel::onSliderPressed);
    connect(slider_, &QSlider::sliderReleased, this, &PlaybackPanel::onSliderReleased);
    connect(slider_, &QSlider::valueChanged, this, &PlaybackPanel::onSliderValueChanged);
    connect(overlayBtn_, &QToolButton::clicked, this, &PlaybackPanel::onToggleOverlay);
    connect(setBgBtn_, &QToolButton::clicked, this, &PlaybackPanel::onSetBackground);

    // Canvas callbacks
    auto *canvas = static_cast<ImageCanvas *>(canvas_);
    canvas->onRoiSelected = [this](const QRect &r)
    {
        roiActive_ = r.isValid() && !r.isNull();
        SPDLOG_INFO("PlaybackPanel: ROI {}",
                    roiActive_ ? fmt::format("x={}, y={}, w={}, h={}", r.x(), r.y(), r.width(), r.height())
                               : std::string("cleared"));
        if (overlayMode_ != OverlayMode::Off)
        {
            computeProcessedOverlay();
        }
        canvas_->update();
    };
    canvas->onRequestBackground = [this]()
    {
        onSetBackground();
    };

    // Timer for periodic refresh (~30 FPS)
    timer_ = new QTimer(this);
    timer_->setInterval(33);
    connect(timer_, &QTimer::timeout, this, &PlaybackPanel::onTick);
    timer_->start();

    updateOverlayButtonUi();
}

PlaybackPanel::~PlaybackPanel() = default;

void PlaybackPanel::onTick()
{
    // Detect capture start transition: resume live follow on start
    const bool running = backend_.capture().isRunning();
    if (running && !prevCaptureRunning_)
    {
        followLive_ = true;
    }
    prevCaptureRunning_ = running;

    // Update slider range from available indices
    uint64_t earliest = 0, latest = 0;
    size_t count = 0;
    const bool hasRange = backend_.playback().queryRange(earliest, latest, count);
    if (hasRange)
    {
        const int minVal = static_cast<int>(std::min<uint64_t>(earliest, static_cast<uint64_t>(std::numeric_limits<int>::max())));
        const int maxVal = static_cast<int>(std::min<uint64_t>(latest, static_cast<uint64_t>(std::numeric_limits<int>::max())));
        if (slider_->minimum() != minVal || slider_->maximum() != maxVal)
        {
            slider_->setRange(minVal, maxVal);
        }
    }

    backend::playback::Frame f;
    bool got = false;
    if (scrubbing_)
    {
        const int val = slider_->value();
        pinnedIndex_ = static_cast<uint64_t>(std::max(0, val));
        got = backend_.playback().fetchByIndex(pinnedIndex_, f);
    }
    else if (followLive_)
    {
        got = backend_.playback().fetchLatest(f);
        if (hasRange)
        {
            const int latestInt = static_cast<int>(std::min<uint64_t>(latest, static_cast<uint64_t>(std::numeric_limits<int>::max())));
            if (slider_->value() != latestInt)
            {
                slider_->setValue(latestInt);
            }
            pinnedIndex_ = static_cast<uint64_t>(latestInt);
        }
    }
    else
    {
        // Review mode: display pinned frame, clamp within available window
        if (hasRange)
        {
            if (pinnedIndex_ < earliest)
                pinnedIndex_ = earliest;
            if (pinnedIndex_ > latest)
                pinnedIndex_ = latest;
            const int pinnedInt = static_cast<int>(std::min<uint64_t>(pinnedIndex_, static_cast<uint64_t>(std::numeric_limits<int>::max())));
            if (slider_->value() != pinnedInt)
                slider_->setValue(pinnedInt);
        }
        got = backend_.playback().fetchByIndex(pinnedIndex_, f);
    }

    if (got)
    {
        // Convert Mono8 to QImage; fallback to grayscale if unknown
        if (f.pixelFormat == 0x01080001 /* PFNC Mono8 */ || true)
        {
            const int w = static_cast<int>(f.width);
            const int h = static_cast<int>(f.height);
            const int pitch = static_cast<int>(f.linePitch == 0 ? f.width : f.linePitch);
            QImage img(f.data.data(), w, h, pitch, QImage::Format_Grayscale8);
            frameImage_ = img.copy(); // ensure ownership
        }
        else
        {
            frameImage_ = QImage();
        }
        if (overlayMode_ != OverlayMode::Off)
        {
            computeProcessedOverlay();
        }
        if (canvas_)
            canvas_->update();
    }

    // Enable/disable background button: allowed when scrubbing or not following live
    if (setBgBtn_)
    {
        const bool captureRunning = backend_.capture().isRunning();
        const bool pausedMode = scrubbing_ || !followLive_ || !captureRunning;
        if (setBgBtn_->isEnabled() != pausedMode)
            setBgBtn_->setEnabled(pausedMode);
    }
}

void PlaybackPanel::onSliderPressed()
{
    scrubbing_ = true;
    SPDLOG_INFO("PlaybackPanel: scrubbing started");
    if (setBgBtn_)
        setBgBtn_->setEnabled(true);
}

void PlaybackPanel::onSliderReleased()
{
    scrubbing_ = false;
    // Stay at user's chosen frame until capture (re)starts
    followLive_ = false;
    SPDLOG_INFO("PlaybackPanel: scrubbing ended, pinned index={}", pinnedIndex_);
    if (setBgBtn_)
        setBgBtn_->setEnabled(true);
}

void PlaybackPanel::onSliderValueChanged(int value)
{
    if (!scrubbing_)
        return; // Only react during scrubbing
    backend::playback::Frame f;
    pinnedIndex_ = static_cast<uint64_t>(std::max(0, value));
    if (backend_.playback().fetchByIndex(pinnedIndex_, f))
    {
        if (f.pixelFormat == 0x01080001 /* PFNC Mono8 */ || true)
        {
            const int w = static_cast<int>(f.width);
            const int h = static_cast<int>(f.height);
            const int pitch = static_cast<int>(f.linePitch == 0 ? f.width : f.linePitch);
            QImage img(f.data.data(), w, h, pitch, QImage::Format_Grayscale8);
            frameImage_ = img.copy();
        }
        else
        {
            frameImage_ = QImage();
        }
        if (overlayMode_ != OverlayMode::Off)
        {
            computeProcessedOverlay();
        }
        if (canvas_)
            canvas_->update();
    }
}

void PlaybackPanel::onToggleOverlay()
{
    switch (overlayMode_)
    {
    case OverlayMode::Off:
        overlayMode_ = OverlayMode::Mask;
        break;
    case OverlayMode::Mask:
        overlayMode_ = OverlayMode::Contours;
        break;
    case OverlayMode::Contours:
        overlayMode_ = OverlayMode::Both;
        break;
    case OverlayMode::Both:
        overlayMode_ = OverlayMode::Off;
        break;
    }
    SPDLOG_INFO("PlaybackPanel: overlay mode changed to {}", static_cast<int>(overlayMode_));
    if (overlayMode_ != OverlayMode::Off)
    {
        computeProcessedOverlay();
    }
    else
    {
        overlayImage_ = QImage();
        overlayContours_.clear();
    }
    updateOverlayButtonUi();
    if (canvas_)
        canvas_->update();
}

void PlaybackPanel::onSetBackground()
{
    const bool pausedMode = scrubbing_ || !followLive_ || !backend_.capture().isRunning();
    if (!pausedMode)
    {
        SPDLOG_INFO("PlaybackPanel: Set Background ignored (not paused)");
        return;
    }
    if (frameImage_.isNull())
        return;
    // Ensure grayscale
    if (frameImage_.format() == QImage::Format_Grayscale8)
        backgroundGray_ = frameImage_.copy();
    else
        backgroundGray_ = frameImage_.convertToFormat(QImage::Format_Grayscale8);
    hasBackground_ = !backgroundGray_.isNull();
    SPDLOG_INFO("PlaybackPanel: background captured ({}x{})",
                backgroundGray_.width(), backgroundGray_.height());
    if (overlayMode_ != OverlayMode::Off)
    {
        computeProcessedOverlay();
        if (canvas_)
            canvas_->update();
    }
}

static cv::Mat qimageToCvGrayClone(const QImage &img)
{
    QImage gray = img.format() == QImage::Format_Grayscale8 ? img : img.convertToFormat(QImage::Format_Grayscale8);
    cv::Mat mat(gray.height(), gray.width(), CV_8UC1,
                const_cast<uchar *>(gray.bits()), gray.bytesPerLine());
    return mat.clone(); // ensure independent buffer
}

static QImage maskToTintedOverlay(const cv::Mat &mask, const QColor &color, int alpha)
{
    const int w = mask.cols;
    const int h = mask.rows;
    QImage overlay(w, h, QImage::Format_RGBA8888);
    overlay.fill(Qt::transparent);
    const uchar r = static_cast<uchar>(color.red());
    const uchar g = static_cast<uchar>(color.green());
    const uchar b = static_cast<uchar>(color.blue());
    const uchar a = static_cast<uchar>(std::clamp(alpha, 0, 255));
    for (int y = 0; y < h; ++y)
    {
        const uchar *mrow = mask.ptr<uchar>(y);
        uchar *orow = overlay.scanLine(y);
        for (int x = 0; x < w; ++x)
        {
            const bool on = mrow[x] > 0;
            if (on)
            {
                orow[4 * x + 0] = b;
                orow[4 * x + 1] = g;
                orow[4 * x + 2] = r;
                orow[4 * x + 3] = a;
            }
            else
            {
                // leave transparent
            }
        }
    }
    return overlay;
}

void PlaybackPanel::computeProcessedOverlay()
{
    overlayImage_ = QImage();
    overlayContours_.clear();
    if (overlayMode_ == OverlayMode::Off)
        return;
    if (frameImage_.isNull())
        return;

    // ROI
    QRect roi = roiActive_ ? imageRoi_ : QRect(0, 0, frameImage_.width(), frameImage_.height());
    roi = roi.intersected(QRect(0, 0, frameImage_.width(), frameImage_.height()));
    if (!roi.isValid() || roi.isNull())
        return;

    // Current frame gray
    cv::Mat current = qimageToCvGrayClone(frameImage_);

    // Prepare background
    cv::Mat bg;
    const bool canUseBg = hasBackground_ &&
                          !backgroundGray_.isNull() &&
                          backgroundGray_.size() == frameImage_.size();
    if (canUseBg)
    {
        bg = qimageToCvGrayClone(backgroundGray_);
    }

    // Process mask in full-size buffer
    cv::Mat mask(current.rows, current.cols, CV_8UC1, cv::Scalar(0));
    cv::Rect cvRoi(roi.x(), roi.y(), roi.width(), roi.height());
    cv::Mat currR = current(cvRoi);
    cv::Mat dstR = mask(cvRoi);
    cv::Mat tmpCurr, tmpBg, diff, thresh;
    // Blur both
    cv::GaussianBlur(currR, tmpCurr, cv::Size(3, 3), 0);
    if (canUseBg)
    {
        cv::Mat bgR = bg(cvRoi);
        cv::GaussianBlur(bgR, tmpBg, cv::Size(3, 3), 0);
        cv::subtract(tmpCurr, tmpBg, diff);
    }
    else
    {
        diff = tmpCurr;
    }
    cv::threshold(diff, thresh, 8, 255, cv::THRESH_BINARY);
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_CROSS, cv::Size(3, 3));
    cv::morphologyEx(thresh, dstR, cv::MORPH_CLOSE, kernel, cv::Point(-1, -1), 1);
    cv::morphologyEx(dstR, dstR, cv::MORPH_OPEN, kernel, cv::Point(-1, -1), 1);

    // Build overlay image
    if (overlayMode_ == OverlayMode::Mask || overlayMode_ == OverlayMode::Both)
    {
        overlayImage_ = maskToTintedOverlay(mask, QColor(0, 255, 0), 90);
    }

    // Build contours
    if (overlayMode_ == OverlayMode::Contours || overlayMode_ == OverlayMode::Both)
    {
        std::vector<std::vector<cv::Point>> contours;
        std::vector<cv::Vec4i> hierarchy;
        cv::findContours(mask, contours, hierarchy, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        overlayContours_.clear();
        overlayContours_.reserve(static_cast<int>(contours.size()));
        for (const auto &c : contours)
        {
            QPolygon poly;
            poly.reserve(static_cast<int>(c.size()));
            for (const auto &pt : c)
            {
                poly << QPoint(pt.x, pt.y);
            }
            overlayContours_.append(poly);
        }
    }
}

void PlaybackPanel::updateOverlayButtonUi()
{
    if (!overlayBtn_)
        return;
    const char *label = nullptr;
    switch (overlayMode_)
    {
    case OverlayMode::Off:
        label = "Overlay: Off";
        break;
    case OverlayMode::Mask:
        label = "Overlay: Mask";
        break;
    case OverlayMode::Contours:
        label = "Overlay: Contours";
        break;
    case OverlayMode::Both:
        label = "Overlay: Both";
        break;
    }
    overlayBtn_->setText(label);
    overlayBtn_->setToolTip("Toggle overlay (Off → Mask → Contours → Both)");
}

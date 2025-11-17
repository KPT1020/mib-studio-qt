#include "frontend/PlaybackPanel.h"

#include <QPainter>
#include <QTimer>
#include <QSlider>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QCursor>
#include <QShortcut>
#include <QKeySequence>
#include <QSize>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <limits>
#include <algorithm>
#include <cmath>

#include "backend/AppBackend.h"
#include "backend/services/CaptureService.h"
#include "backend/services/PlaybackService.h"
#include "backend/services/ProcessingService.h"
#include "backend/playback/FrameStore.h"
#include "backend/Tools.h"
#include "frontend/BufferSaveDialog.h"

#include <spdlog/spdlog.h>
#include <fmt/format.h>

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
    slider_->setFocusPolicy(Qt::StrongFocus);
    slider_->installEventFilter(this);

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
    saveBufferBtn_ = new QToolButton(controls);
    saveBufferBtn_->setText("Save Buffer");
    saveBufferBtn_->setToolTip("Save buffer frames to disk and manage buffer size");
    controlsLayout->addWidget(overlayBtn_);
    controlsLayout->addWidget(setBgBtn_);
    controlsLayout->addWidget(saveBufferBtn_);
    controlsLayout->addStretch(1);
    layout->addWidget(controls);

    layout->addWidget(slider_);

    connect(slider_, &QSlider::sliderPressed, this, &PlaybackPanel::onSliderPressed);
    connect(slider_, &QSlider::sliderReleased, this, &PlaybackPanel::onSliderReleased);
    connect(slider_, &QSlider::valueChanged, this, &PlaybackPanel::onSliderValueChanged);
    connect(overlayBtn_, &QToolButton::clicked, this, &PlaybackPanel::onToggleOverlay);
    connect(setBgBtn_, &QToolButton::clicked, this, &PlaybackPanel::onSetBackground);
    connect(saveBufferBtn_, &QToolButton::clicked, this, &PlaybackPanel::onSaveBuffer);

    // Space shortcut to start/stop capture
    {
        auto *spaceShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
        connect(spaceShortcut, &QShortcut::activated, this, &PlaybackPanel::onToggleCapture);
    }

    // Canvas callbacks
    auto *canvas = static_cast<ImageCanvas *>(canvas_);
    canvas->onRoiSelected = [this](const QRect &r)
    {
        roiActive_ = r.isValid() && !r.isNull();
        SPDLOG_INFO("PlaybackPanel: ROI {}",
                    roiActive_ ? fmt::format("x={}, y={}, w={}, h={}", r.x(), r.y(), r.width(), r.height())
                               : std::string("cleared"));
        // Sync ROI to backend realtime processor (full image when cleared)
        backend::services::ProcessingService::Roi roi{};
        if (roiActive_)
        {
            roi.x = r.x();
            roi.y = r.y();
            roi.w = r.width();
            roi.h = r.height();
        }
        else
        {
            roi.x = 0;
            roi.y = 0;
            roi.w = frameImage_.width();
            roi.h = frameImage_.height();
        }
        backend_.processing().setRealtimeRoi(roi);
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

    // Timer for periodic refresh (configurable display_fps, default 60 Hz)
    int displayFps = 60;
    {
        QFile f(":/defaults/config.json");
        if (f.open(QIODevice::ReadOnly)) {
            const QByteArray data = f.readAll();
            const QJsonDocument doc = QJsonDocument::fromJson(data);
            if (doc.isObject()) {
                const QJsonObject obj = doc.object();
                if (obj.contains("display_fps"))
                    displayFps = obj.value("display_fps").toInt(displayFps);
            }
        }
    }
    if (displayFps < 1) displayFps = 1;
    if (displayFps > 240) displayFps = 240;
    const int intervalMs = std::max(1, static_cast<int>(std::lround(1000.0 / static_cast<double>(displayFps))));

    timer_ = new QTimer(this);
    timer_->setTimerType(Qt::PreciseTimer);
    timer_->setInterval(intervalMs);
    connect(timer_, &QTimer::timeout, this, &PlaybackPanel::onTick);
    timer_->start();
    SPDLOG_INFO("Playback preview: display_fps={} (~{} ms)", displayFps, intervalMs);

    // Timer for periodic metrics logging (1 second interval)
    metricsTimer_ = new QTimer(this);
    metricsTimer_->setInterval(1000);
    connect(metricsTimer_, &QTimer::timeout, this, &PlaybackPanel::onLogMetrics);
    metricsTimer_->start();

    resetMetrics();
    updateOverlayButtonUi();
}

PlaybackPanel::~PlaybackPanel() = default;

void PlaybackPanel::setRoi(const QRect& roi) {
    imageRoi_ = roi;
    roiActive_ = roi.isValid() && !roi.isNull();
    SPDLOG_INFO("PlaybackPanel: ROI {}",
                roiActive_ ? fmt::format("x={}, y={}, w={}, h={}", roi.x(), roi.y(), roi.width(), roi.height())
                           : std::string("cleared"));
    // Sync ROI to backend realtime processor (full image when cleared)
    backend::services::ProcessingService::Roi backendRoi{};
    if (roiActive_)
    {
        backendRoi.x = roi.x();
        backendRoi.y = roi.y();
        backendRoi.w = roi.width();
        backendRoi.h = roi.height();
    }
    else
    {
        backendRoi.x = 0;
        backendRoi.y = 0;
        backendRoi.w = frameImage_.width();
        backendRoi.h = frameImage_.height();
    }
    backend_.processing().setRealtimeRoi(backendRoi);
    if (overlayMode_ != OverlayMode::Off)
    {
        computeProcessedOverlay();
    }
    if (canvas_)
        canvas_->update();
}

QRect PlaybackPanel::getRoi() const {
    return imageRoi_;
}

QSize PlaybackPanel::getImageDimensions() const {
    if (frameImage_.isNull())
        return QSize(0, 0);
    return frameImage_.size();
}

void PlaybackPanel::onTick()
{
    // Detect capture start/stop transitions
    const bool running = backend_.capture().isRunning();
    if (running && !prevCaptureRunning_)
    {
        followLive_ = true;
        resetMetrics(); // Reset metrics when capture starts
    }
    else if (!running && prevCaptureRunning_)
    {
        resetMetrics(); // Reset metrics when capture stops
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

        // Track metrics for live playback only (not scrubbing or review mode)
        if (running && followLive_ && !scrubbing_ && hasRange)
        {
            const uint64_t displayTimeUs = backend::Tools::getTimestamp();
            // Use latest index when following live (only track if range is available)
            const uint64_t frameIndex = latest;
            // Only track if this is a new frame (index changed) to avoid duplicate samples
            // Display FPS will be calculated from actual frame updates, not just UI refreshes
            if (!metricsInitialized_ || frameIndex != lastDisplayedIndex_)
            {
                // Frame timestamp may be in nanoseconds, convert to microseconds for consistency
                // Assuming frame timestamps are in nanoseconds (common for Euresys SDK)
                uint64_t frameTimestampUs = f.timestamp;
                // If timestamp is likely in nanoseconds (> 1e12), convert to microseconds
                if (f.timestamp > 1'000'000'000'000ULL)
                {
                    frameTimestampUs = f.timestamp / 1000ULL;
                }
                trackFrameDisplay(frameIndex, frameTimestampUs, displayTimeUs);
            }
        }
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
    if (slider_)
        slider_->setFocus();
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
    // Also push background to backend realtime processor
    if (!backgroundGray_.isNull())
    {
        QImage gray = backgroundGray_.format() == QImage::Format_Grayscale8 ? backgroundGray_
                                                                            : backgroundGray_.convertToFormat(QImage::Format_Grayscale8);
        cv::Mat bg(gray.height(), gray.width(), CV_8UC1, const_cast<uchar *>(gray.bits()), gray.bytesPerLine());
        backend_.processing().setRealtimeBackgroundGray(bg.clone());
    }
}

void PlaybackPanel::onSaveBuffer()
{
    frontend::BufferSaveDialog dialog(backend_, this);
    dialog.exec();
}

void PlaybackPanel::onToggleCapture()
{
    if (backend_.capture().isRunning())
    {
        SPDLOG_INFO("PlaybackPanel: stopping capture (Space)");
        backend_.capture().stop();
    }
    else
    {
        SPDLOG_INFO("PlaybackPanel: starting capture (Space)");
        backend_.capture().start();
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

bool PlaybackPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == slider_ && event->type() == QEvent::KeyPress)
    {
        auto *ke = static_cast<QKeyEvent *>(event);
        const int key = ke->key();
        const bool shift = (ke->modifiers() & Qt::ShiftModifier);
        const int step = shift ? std::max(1, slider_->pageStep()) : std::max(1, slider_->singleStep());
        const int minVal = slider_->minimum();
        const int maxVal = slider_->maximum();
        int newVal = slider_->value();
        bool handled = false;

        switch (key)
        {
        case Qt::Key_Left:
            newVal = std::clamp(newVal - step, minVal, maxVal);
            handled = true;
            break;
        case Qt::Key_Right:
            newVal = std::clamp(newVal + step, minVal, maxVal);
            handled = true;
            break;
        case Qt::Key_Home:
            newVal = minVal;
            handled = true;
            break;
        case Qt::Key_End:
            newVal = maxVal;
            handled = true;
            break;
        case Qt::Key_Space:
            onToggleCapture();
            return true;
        default:
            break;
        }

        if (handled)
        {
            followLive_ = false;
            if (setBgBtn_)
                setBgBtn_->setEnabled(true);
            if (newVal != slider_->value())
            {
                scrubbing_ = true;
                slider_->setValue(newVal); // triggers onSliderValueChanged
                scrubbing_ = false;
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void PlaybackPanel::resetMetrics() {
    metricsWindow_.clear();
    lastDisplayedIndex_ = 0;
    lastDisplayTimeUs_ = 0;
    metricsInitialized_ = false;
    totalDrops_ = 0;
}

void PlaybackPanel::trackFrameDisplay(uint64_t frameIndex, uint64_t frameTimestamp, uint64_t displayTime) {
    const uint64_t windowDurationUs = 1'000'000ULL; // 1 second in microseconds

    // Prune old samples outside the 1-second window
    while (!metricsWindow_.empty() && (displayTime - metricsWindow_.front().displayTimeUs) > windowDurationUs) {
        metricsWindow_.pop_front();
    }

    // Detect frame drops by comparing sequential indices
    if (metricsInitialized_) {
        if (frameIndex > lastDisplayedIndex_ + 1) {
            // Frames were skipped
            const uint64_t drops = frameIndex - lastDisplayedIndex_ - 1;
            totalDrops_ += drops;
        }
    } else {
        metricsInitialized_ = true;
    }

    // Add new sample to window
    MetricsSample sample;
    sample.displayTimeUs = displayTime;
    sample.frameTimestamp = frameTimestamp;
    sample.frameIndex = frameIndex;
    metricsWindow_.push_back(sample);

    lastDisplayedIndex_ = frameIndex;
    lastDisplayTimeUs_ = displayTime;
}

void PlaybackPanel::onLogMetrics() {
    const bool captureRunning = backend_.capture().isRunning();
    
    // Only log metrics during live playback when capture is running
    if (!captureRunning || !followLive_ || scrubbing_) {
        return;
    }

    if (metricsWindow_.empty()) {
        return;
    }

    const uint64_t nowUs = backend::Tools::getTimestamp();
    const uint64_t windowDurationUs = 1'000'000ULL; // 1 second

    // Prune samples outside the window
    while (!metricsWindow_.empty() && (nowUs - metricsWindow_.front().displayTimeUs) > windowDurationUs) {
        metricsWindow_.pop_front();
    }

    if (metricsWindow_.empty()) {
        return;
    }

    // Calculate display FPS: number of frames displayed in the window
    const size_t displayCount = metricsWindow_.size();
    const uint64_t windowStartUs = metricsWindow_.front().displayTimeUs;
    const uint64_t windowEndUs = metricsWindow_.back().displayTimeUs;
    const double windowDurationSeconds = (windowEndUs > windowStartUs) 
        ? static_cast<double>(windowEndUs - windowStartUs) / 1'000'000.0 
        : 1.0; // Fallback to 1 second if timestamps are equal
    
    const double displayFps = static_cast<double>(displayCount) / windowDurationSeconds;

    // Calculate average latency
    // Note: Frame timestamps from camera may be in nanoseconds, display time is in microseconds
    // We'll assume frame timestamps are also in microseconds for latency calculation
    // If they're in nanoseconds, we'd need to divide by 1000
    double totalLatencyUs = 0.0;
    size_t validLatencySamples = 0;
    
    for (const auto& sample : metricsWindow_) {
        // Calculate latency: display_time - capture_time
        // Handle potential overflow/wraparound by checking if display time is after capture time
        if (sample.displayTimeUs >= sample.frameTimestamp) {
            const double latencyUs = static_cast<double>(sample.displayTimeUs - sample.frameTimestamp);
            totalLatencyUs += latencyUs;
            validLatencySamples++;
        }
    }

    const double avgLatencyMs = (validLatencySamples > 0) 
        ? (totalLatencyUs / static_cast<double>(validLatencySamples)) / 1000.0 
        : 0.0;

    // Get drops in the current window (recent drops)
    uint64_t windowDrops = 0;
    if (metricsWindow_.size() > 1) {
        auto it = metricsWindow_.begin();
        auto prev = it++;
        for (; it != metricsWindow_.end(); ++it, ++prev) {
            if (it->frameIndex > prev->frameIndex + 1) {
                windowDrops += (it->frameIndex - prev->frameIndex - 1);
            }
        }
    }

    SPDLOG_INFO("Playback metrics: display_fps={:.1f}, avg_latency_ms={:.2f}, drops={} (window_drops={})",
                displayFps, avgLatencyMs, totalDrops_, windowDrops);
}

#include "frontend/PlaybackPanel.h"

#include <QPainter>
#include <QTimer>
#include <QSlider>
#include <QVBoxLayout>
#include <limits>
#include <algorithm>

#include "backend/AppBackend.h"
#include "backend/services/CaptureService.h"
#include "backend/services/PlaybackService.h"
#include "backend/playback/FrameStore.h"

#include <spdlog/spdlog.h>

namespace
{

    class ImageCanvas : public QWidget
    {
    public:
        explicit ImageCanvas(QImage *image, QWidget *parent = nullptr)
            : QWidget(parent), image_(image) {}

    protected:
        void paintEvent(QPaintEvent *) override
        {
            QPainter p(this);
            p.fillRect(rect(), Qt::black);
            if (image_ && !image_->isNull())
            {
                QImage scaled = image_->scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                QPoint topLeft((width() - scaled.width()) / 2, (height() - scaled.height()) / 2);
                p.drawImage(topLeft, scaled);
            }
        }

    private:
        QImage *image_ = nullptr;
    };

} // namespace

PlaybackPanel::PlaybackPanel(backend::AppBackend &backend, QWidget *parent)
    : QWidget(parent), backend_(backend)
{
    // UI: image canvas + horizontal slider
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    canvas_ = new ImageCanvas(&frameImage_, this);
    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setRange(0, 0);
    slider_->setSingleStep(1);
    slider_->setPageStep(8);

    layout->addWidget(canvas_, 1);
    layout->addWidget(slider_);

    connect(slider_, &QSlider::sliderPressed, this, &PlaybackPanel::onSliderPressed);
    connect(slider_, &QSlider::sliderReleased, this, &PlaybackPanel::onSliderReleased);
    connect(slider_, &QSlider::valueChanged, this, &PlaybackPanel::onSliderValueChanged);

    // Timer for periodic refresh (~30 FPS)
    timer_ = new QTimer(this);
    timer_->setInterval(33);
    connect(timer_, &QTimer::timeout, this, &PlaybackPanel::onTick);
    timer_->start();
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
        if (canvas_)
            canvas_->update();
    }
}

void PlaybackPanel::onSliderPressed()
{
    scrubbing_ = true;
    SPDLOG_INFO("PlaybackPanel: scrubbing started");
}

void PlaybackPanel::onSliderReleased()
{
    scrubbing_ = false;
    // Stay at user's chosen frame until capture (re)starts
    followLive_ = false;
    SPDLOG_INFO("PlaybackPanel: scrubbing ended, pinned index={}", pinnedIndex_);
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
        if (canvas_)
            canvas_->update();
    }
}

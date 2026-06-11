#include "frontend/utils/BackgroundPreviewWidget.h"

#include <QLabel>
#include <QPalette>
#include <QVBoxLayout>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QSizePolicy>

namespace frontend
{

    BackgroundPreviewWidget::BackgroundPreviewWidget(QWidget* parent)
        : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setMinimumHeight(100);
        setMaximumHeight(300);
        setAutoFillBackground(true);
        setBackgroundRole(QPalette::Window);

        layout_ = new QVBoxLayout(this);
        layout_->setContentsMargins(4, 4, 4, 4);
        layout_->setSpacing(2);

        // Text label for dimensions/status
        textLabel_ = new QLabel(this);
        textLabel_->setAlignment(Qt::AlignCenter);
        textLabel_->setText("No background set");
        textLabel_->setBackgroundRole(QPalette::Window);
        textLabel_->setAutoFillBackground(true);
        textLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        textLabel_->setMaximumHeight(20);
        layout_->addWidget(textLabel_);

        // Image label for preview
        imageLabel_ = new QLabel(this);
        imageLabel_->setAlignment(Qt::AlignCenter);
        imageLabel_->setBackgroundRole(QPalette::Base);
        imageLabel_->setAutoFillBackground(true);
        imageLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        imageLabel_->setMinimumHeight(80);
        layout_->addWidget(imageLabel_);
    }

    void BackgroundPreviewWidget::setBackgroundImage(const QImage& image)
    {
        if (image.isNull())
        {
            clearBackground();
            return;
        }

        backgroundImage_ = image;
        hasImage_ = true;
        updatePreview();
    }

    void BackgroundPreviewWidget::clearBackground()
    {
        backgroundImage_ = QImage();
        scaledPreview_ = QImage();
        hasImage_ = false;
        textLabel_->setText("No background set");
        imageLabel_->setPixmap(QPixmap());
        update();
    }

    void BackgroundPreviewWidget::updatePreview()
    {
        if (!hasImage_ || backgroundImage_.isNull())
        {
            textLabel_->setText("No background set");
            imageLabel_->setPixmap(QPixmap());
            return;
        }

        // Update text label with dimensions
        QString text = QString("Background: %1x%2")
                           .arg(backgroundImage_.width())
                           .arg(backgroundImage_.height());
        textLabel_->setText(text);

        // Scale image to fit width while maintaining aspect ratio
        int availableWidth = width() - 8; // Account for margins
        int availableHeight = imageLabel_->height();

        if (availableWidth > 0 && availableHeight > 0)
        {
            QSize scaledSize = backgroundImage_.size().scaled(
                availableWidth, availableHeight,
                Qt::KeepAspectRatio);

            scaledPreview_ = backgroundImage_.scaled(
                scaledSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

            imageLabel_->setPixmap(QPixmap::fromImage(scaledPreview_));
        }
    }

    void BackgroundPreviewWidget::paintEvent(QPaintEvent* event)
    {
        QWidget::paintEvent(event);
    }

    void BackgroundPreviewWidget::resizeEvent(QResizeEvent* event)
    {
        QWidget::resizeEvent(event);
        if (hasImage_)
        {
            updatePreview();
        }
    }

} // namespace frontend

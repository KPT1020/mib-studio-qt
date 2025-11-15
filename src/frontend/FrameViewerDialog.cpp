#include "frontend/FrameViewerDialog.h"

#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPainter>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QMessageBox>
#include <QEvent>

#include "backend/services/ProcessingService.h"
#include <spdlog/spdlog.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace frontend {

FrameViewerDialog::FrameViewerDialog(const backend::services::ProcessedFrame& frame,
                                     const backend::services::ProcessingService::Roi& roi,
                                     bool showOverlays,
                                     QWidget* parent)
    : QDialog(parent),
      frame_(&frame),
      roi_(roi),
      showProcessingOverlay_(showOverlays),
      showRoiOverlay_(showOverlays),
      zoomFactor_(1.0)
{
    setWindowTitle(tr("Frame Viewer - Frame %1").arg(frame.index));
    setModal(true);
    resize(1200, 800);
    
    // Set initial zoom to fit window
    zoomFactor_ = 1.0;

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(6);
    mainLayout->setContentsMargins(6, 6, 6, 6);

    // Frame info panel
    frameInfoLabel_ = new QLabel(this);
    frameInfoLabel_->setStyleSheet("QLabel { background-color: #f0f0f0; padding: 4px; border: 1px solid #ccc; }");
    frameInfoLabel_->setWordWrap(true);
    mainLayout->addWidget(frameInfoLabel_);

    // Image display area
    imageScrollArea_ = new QScrollArea(this);
    imageScrollArea_->setWidgetResizable(true);
    imageScrollArea_->setBackgroundRole(QPalette::Dark);
    imageScrollArea_->setAlignment(Qt::AlignCenter);
    imageScrollArea_->installEventFilter(this);
    
    imageLabel_ = new QLabel(this);
    imageLabel_->setAlignment(Qt::AlignCenter);
    imageLabel_->setBackgroundRole(QPalette::Base);
    imageLabel_->setScaledContents(false);
    imageLabel_->setMinimumSize(400, 300);
    
    imageScrollArea_->setWidget(imageLabel_);
    mainLayout->addWidget(imageScrollArea_, 1);

    // Controls layout
    auto* controlsLayout = new QHBoxLayout();
    
    // Navigation buttons
    prevButton_ = new QPushButton(tr("◀ Previous"), this);
    nextButton_ = new QPushButton(tr("Next ▶"), this);
    controlsLayout->addWidget(prevButton_);
    controlsLayout->addWidget(nextButton_);
    
    controlsLayout->addSpacing(20);
    
    // Overlay checkboxes
    processingOverlayCheck_ = new QCheckBox(tr("Processing Overlay"), this);
    processingOverlayCheck_->setChecked(showProcessingOverlay_);
    roiOverlayCheck_ = new QCheckBox(tr("ROI Overlay"), this);
    roiOverlayCheck_->setChecked(showRoiOverlay_);
    controlsLayout->addWidget(processingOverlayCheck_);
    controlsLayout->addWidget(roiOverlayCheck_);
    
    controlsLayout->addSpacing(20);
    
    // Zoom controls
    zoomOutButton_ = new QPushButton(tr("Zoom Out"), this);
    fitToWindowButton_ = new QPushButton(tr("Fit to Window"), this);
    zoomInButton_ = new QPushButton(tr("Zoom In"), this);
    controlsLayout->addWidget(zoomOutButton_);
    controlsLayout->addWidget(fitToWindowButton_);
    controlsLayout->addWidget(zoomInButton_);
    
    controlsLayout->addStretch();
    
    // Close button
    closeButton_ = new QPushButton(tr("Close"), this);
    closeButton_->setDefault(true);
    controlsLayout->addWidget(closeButton_);
    
    mainLayout->addLayout(controlsLayout);

    // Connect signals
    connect(prevButton_, &QPushButton::clicked, this, &FrameViewerDialog::onPreviousFrame);
    connect(nextButton_, &QPushButton::clicked, this, &FrameViewerDialog::onNextFrame);
    connect(processingOverlayCheck_, &QCheckBox::toggled, this, &FrameViewerDialog::onToggleProcessingOverlay);
    connect(roiOverlayCheck_, &QCheckBox::toggled, this, &FrameViewerDialog::onToggleRoiOverlay);
    connect(zoomInButton_, &QPushButton::clicked, this, &FrameViewerDialog::onZoomIn);
    connect(zoomOutButton_, &QPushButton::clicked, this, &FrameViewerDialog::onZoomOut);
    connect(fitToWindowButton_, &QPushButton::clicked, this, &FrameViewerDialog::onFitToWindow);
    connect(closeButton_, &QPushButton::clicked, this, &QDialog::accept);

    // Update display
    updateImage();
    updateFrameInfo();
}

void FrameViewerDialog::setFrame(const backend::services::ProcessedFrame& frame) {
    frame_ = &frame;
    setWindowTitle(tr("Frame Viewer - Frame %1").arg(frame.index));
    updateImage();
    updateFrameInfo();
}

void FrameViewerDialog::setRoi(const backend::services::ProcessingService::Roi& roi) {
    roi_ = roi;
    updateImage();
}

void FrameViewerDialog::setShowOverlays(bool show) {
    showProcessingOverlay_ = show;
    showRoiOverlay_ = show;
    processingOverlayCheck_->setChecked(show);
    roiOverlayCheck_->setChecked(show);
    updateImage();
}

void FrameViewerDialog::onToggleProcessingOverlay(bool enabled) {
    showProcessingOverlay_ = enabled;
    updateImage();
}

void FrameViewerDialog::onToggleRoiOverlay(bool enabled) {
    showRoiOverlay_ = enabled;
    updateImage();
}

void FrameViewerDialog::onPreviousFrame() {
    emit requestPreviousFrame();
}

void FrameViewerDialog::onNextFrame() {
    emit requestNextFrame();
}

void FrameViewerDialog::onZoomIn() {
    zoomFactor_ *= 1.2;
    updateImage();
}

void FrameViewerDialog::onZoomOut() {
    zoomFactor_ /= 1.2;
    if (zoomFactor_ < 0.1) zoomFactor_ = 0.1;
    updateImage();
}

void FrameViewerDialog::onFitToWindow() {
    zoomFactor_ = 1.0;
    updateImage();
}

bool FrameViewerDialog::eventFilter(QObject* obj, QEvent* event) {
    if (obj == imageScrollArea_ && event->type() == QEvent::Wheel) {
        QWheelEvent* wheelEvent = static_cast<QWheelEvent*>(event);
        if (wheelEvent->modifiers() & Qt::ControlModifier) {
            double delta = wheelEvent->angleDelta().y() / 120.0;
            zoomFactor_ *= (1.0 + delta * 0.1);
            if (zoomFactor_ < 0.1) zoomFactor_ = 0.1;
            if (zoomFactor_ > 10.0) zoomFactor_ = 10.0;
            updateImage();
            return true;
        }
    }
    return QDialog::eventFilter(obj, event);
}

void FrameViewerDialog::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Up) {
        onPreviousFrame();
        event->accept();
    } else if (event->key() == Qt::Key_Right || event->key() == Qt::Key_Down) {
        onNextFrame();
        event->accept();
    } else if (event->key() == Qt::Key_Escape) {
        accept();
        event->accept();
    } else {
        QDialog::keyPressEvent(event);
    }
}

void FrameViewerDialog::updateImage() {
    if (!frame_ || frame_->originalImage.empty()) {
        displayImage_ = QImage();
        imageLabel_->setPixmap(QPixmap());
        return;
    }

    QImage baseImage;

    // Apply processing overlay if enabled
    if (showProcessingOverlay_ && !frame_->processedImage.empty()) {
        baseImage = createProcessingOverlay(frame_->originalImage, frame_->processedImage);
    } else {
        baseImage = matToQImage(frame_->originalImage);
    }

    // Apply ROI rectangle overlay if enabled
    if (showRoiOverlay_ && roi_.w > 0 && roi_.h > 0 && !baseImage.isNull()) {
        QImage overlayImage = baseImage.copy();
        QPainter painter(&overlayImage);
        painter.setRenderHint(QPainter::Antialiasing);

        // Draw ROI rectangle
        QPen pen(QColor(255, 0, 0), 3);
        painter.setPen(pen);
        painter.drawRect(roi_.x, roi_.y, roi_.w, roi_.h);

        baseImage = overlayImage;
    }

    // Apply zoom
    if (zoomFactor_ != 1.0 && !baseImage.isNull()) {
        QSize newSize(static_cast<int>(baseImage.width() * zoomFactor_), 
                     static_cast<int>(baseImage.height() * zoomFactor_));
        displayImage_ = baseImage.scaled(newSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    } else {
        displayImage_ = baseImage;
    }

    // Update label
    if (!displayImage_.isNull()) {
        imageLabel_->setPixmap(QPixmap::fromImage(displayImage_));
        imageLabel_->resize(displayImage_.size());
        // Ensure scroll area shows the image properly
        imageScrollArea_->ensureWidgetVisible(imageLabel_);
    } else {
        imageLabel_->setPixmap(QPixmap());
    }
}

QImage FrameViewerDialog::createProcessingOverlay(const cv::Mat& original, const cv::Mat& mask) const {
    if (original.empty() || mask.empty()) {
        return matToQImage(original);
    }
    
    // Convert original to RGB if needed
    cv::Mat rgb;
    if (original.channels() == 1) {
        cv::cvtColor(original, rgb, cv::COLOR_GRAY2RGB);
    } else {
        rgb = original.clone();
        if (rgb.channels() == 3) {
            cv::cvtColor(rgb, rgb, cv::COLOR_BGR2RGB);
        }
    }
    
    // Create overlay: green tint where mask is non-zero
    cv::Mat overlay = rgb.clone();
    for (int y = 0; y < overlay.rows && y < mask.rows; ++y) {
        for (int x = 0; x < overlay.cols && x < mask.cols; ++x) {
            if (mask.at<uchar>(y, x) > 0) {
                cv::Vec3b& pixel = overlay.at<cv::Vec3b>(y, x);
                // Blend with green (0, 255, 0) at 30% opacity
                pixel[0] = static_cast<uchar>(pixel[0] * 0.7); // R
                pixel[1] = static_cast<uchar>(std::min(255.0, pixel[1] * 0.7 + 255.0 * 0.3)); // G
                pixel[2] = static_cast<uchar>(pixel[2] * 0.7); // B
            }
        }
    }
    
    QImage img(overlay.data, overlay.cols, overlay.rows, static_cast<int>(overlay.step), QImage::Format_RGB888);
    return img.copy();
}

QImage FrameViewerDialog::matToQImage(const cv::Mat& mat) const {
    if (mat.empty()) {
        return QImage();
    }

    if (mat.type() == CV_8UC1) {
        // Grayscale
        QImage img(mat.data, mat.cols, mat.rows, static_cast<int>(mat.step), QImage::Format_Grayscale8);
        return img.copy();
    } else if (mat.type() == CV_8UC3) {
        // BGR to RGB
        cv::Mat rgb;
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
        QImage img(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888);
        return img.copy();
    } else if (mat.type() == CV_8UC4) {
        // BGRA to RGBA
        cv::Mat rgba;
        cv::cvtColor(mat, rgba, cv::COLOR_BGRA2RGBA);
        QImage img(rgba.data, rgba.cols, rgba.rows, static_cast<int>(rgba.step), QImage::Format_RGBA8888);
        return img.copy();
    }

    // Fallback: convert to grayscale
    cv::Mat gray;
    cv::cvtColor(mat, gray, cv::COLOR_BGR2GRAY);
    QImage img(gray.data, gray.cols, gray.rows, static_cast<int>(gray.step), QImage::Format_Grayscale8);
    return img.copy();
}

void FrameViewerDialog::updateFrameInfo() {
    if (!frame_) {
        frameInfoLabel_->setText(tr("No frame selected"));
        return;
    }

    const auto& val = frame_->validation;
    QString info = QString(
        "<b>Frame Index:</b> %1 | "
        "<b>Timestamp:</b> %2 ns | "
        "<b>Deformability:</b> %3 | "
        "<b>Area:</b> %4 | "
        "<b>Area Ratio:</b> %5 | "
        "<b>Ring Ratio:</b> %6 | "
        "<b>Valid:</b> %7 | "
        "<b>Inner Count:</b> %8"
    )
    .arg(frame_->index)
    .arg(frame_->timestampNs)
    .arg(val.deformability, 0, 'f', 3)
    .arg(val.area, 0, 'f', 2)
    .arg(val.areaRatio, 0, 'f', 3)
    .arg(val.ringRatio, 0, 'f', 3)
    .arg(val.isValid ? "Yes" : "No")
    .arg(val.innerContourCount);

    frameInfoLabel_->setText(info);
}

} // namespace frontend


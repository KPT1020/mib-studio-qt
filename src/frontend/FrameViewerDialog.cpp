#include "frontend/FrameViewerDialog.h"
#include "ui_FrameViewerDialog.h"

#include <QPainter>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QMessageBox>
#include <QEvent>
#include <QFileDialog>

#include "backend/services/ProcessingService.h"
#include <spdlog/spdlog.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

namespace frontend {

FrameViewerDialog::FrameViewerDialog(const backend::services::ProcessedFrame& frame,
                                     const backend::services::ProcessingService::Roi& roi,
                                     bool showOverlays,
                                     QWidget* parent)
    : QDialog(parent),
      ui(new Ui::FrameViewerDialog),
      frame_(&frame),
      roi_(roi),
      showProcessingOverlay_(showOverlays),
      showRoiOverlay_(showOverlays),
      zoomFactor_(1.0)
{
    ui->setupUi(this);
    setWindowTitle(tr("Frame Viewer - Frame %1").arg(frame.index));
    
    // Set initial zoom to fit window
    zoomFactor_ = 1.0;

    // Configure scroll area
    ui->imageScrollArea->setBackgroundRole(QPalette::Dark);
    ui->imageScrollArea->installEventFilter(this);
    
    // Configure image label
    ui->imageLabel->setBackgroundRole(QPalette::Base);
    
    // Set overlay checkboxes
    ui->processingOverlayCheck->setChecked(showProcessingOverlay_);
    ui->roiOverlayCheck->setChecked(showRoiOverlay_);

    // Connect signals
    connect(ui->prevButton, &QPushButton::clicked, this, &FrameViewerDialog::onPreviousFrame);
    connect(ui->nextButton, &QPushButton::clicked, this, &FrameViewerDialog::onNextFrame);
    connect(ui->processingOverlayCheck, &QCheckBox::toggled, this, &FrameViewerDialog::onToggleProcessingOverlay);
    connect(ui->roiOverlayCheck, &QCheckBox::toggled, this, &FrameViewerDialog::onToggleRoiOverlay);
    connect(ui->zoomInButton, &QPushButton::clicked, this, &FrameViewerDialog::onZoomIn);
    connect(ui->zoomOutButton, &QPushButton::clicked, this, &FrameViewerDialog::onZoomOut);
    connect(ui->fitToWindowButton, &QPushButton::clicked, this, &FrameViewerDialog::onFitToWindow);
    connect(ui->exportButton, &QPushButton::clicked, this, &FrameViewerDialog::onExportFrame);
    connect(ui->closeButton, &QPushButton::clicked, this, &QDialog::accept);

    // Update display
    updateImage();
    updateFrameInfo();
}

FrameViewerDialog::~FrameViewerDialog() {
    delete ui;
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
    ui->processingOverlayCheck->setChecked(show);
    ui->roiOverlayCheck->setChecked(show);
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
    if (obj == ui->imageScrollArea && event->type() == QEvent::Wheel) {
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
        ui->imageLabel->setPixmap(QPixmap());
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
        ui->imageLabel->setPixmap(QPixmap::fromImage(displayImage_));
        ui->imageLabel->resize(displayImage_.size());
        // Ensure scroll area shows the image properly
        ui->imageScrollArea->ensureWidgetVisible(ui->imageLabel);
    } else {
        ui->imageLabel->setPixmap(QPixmap());
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
        ui->frameInfoLabel->setText(tr("No frame selected"));
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

    ui->frameInfoLabel->setText(info);
}

void FrameViewerDialog::onExportFrame() {
    if (!frame_ || frame_->originalImage.empty()) {
        QMessageBox::warning(this, tr("Export Error"),
                            tr("No frame data available to export."));
        return;
    }

    QString filePath = QFileDialog::getSaveFileName(this, tr("Export Frame as TIFF"),
                                                   QString("frame_%1.tiff").arg(frame_->index),
                                                   tr("TIFF Files (*.tiff *.tif);;All Files (*)"));
    if (filePath.isEmpty()) {
        return;
    }

    // Ensure .tiff extension
    if (!filePath.endsWith(".tiff", Qt::CaseInsensitive) && !filePath.endsWith(".tif", Qt::CaseInsensitive)) {
        filePath += ".tiff";
    }

    // Prepare image for export (use original, not zoomed display)
    cv::Mat exportImage;
    
    if (showProcessingOverlay_ && !frame_->processedImage.empty()) {
        // Create overlay image
        if (frame_->originalImage.channels() == 1) {
            cv::cvtColor(frame_->originalImage, exportImage, cv::COLOR_GRAY2BGR);
        } else {
            exportImage = frame_->originalImage.clone();
            if (exportImage.channels() == 3) {
                // Already BGR, keep as is
            } else if (exportImage.channels() == 4) {
                cv::cvtColor(exportImage, exportImage, cv::COLOR_BGRA2BGR);
            }
        }
        
        // Apply processing overlay (green tint where mask is non-zero)
        for (int y = 0; y < exportImage.rows && y < frame_->processedImage.rows; ++y) {
            for (int x = 0; x < exportImage.cols && x < frame_->processedImage.cols; ++x) {
                if (frame_->processedImage.at<uchar>(y, x) > 0) {
                    cv::Vec3b& pixel = exportImage.at<cv::Vec3b>(y, x);
                    // Blend with green (0, 255, 0) at 30% opacity
                    pixel[0] = static_cast<uchar>(pixel[0] * 0.7); // B
                    pixel[1] = static_cast<uchar>(std::min(255.0, pixel[1] * 0.7 + 255.0 * 0.3)); // G
                    pixel[2] = static_cast<uchar>(pixel[2] * 0.7); // R
                }
            }
        }
    } else {
        // Export original image
        if (frame_->originalImage.channels() == 1) {
            exportImage = frame_->originalImage.clone();
        } else if (frame_->originalImage.channels() == 3) {
            exportImage = frame_->originalImage.clone();
        } else if (frame_->originalImage.channels() == 4) {
            cv::cvtColor(frame_->originalImage, exportImage, cv::COLOR_BGRA2BGR);
        } else {
            // Convert to grayscale as fallback
            cv::cvtColor(frame_->originalImage, exportImage, cv::COLOR_BGR2GRAY);
        }
    }

    // Draw ROI rectangle if enabled
    if (showRoiOverlay_ && roi_.w > 0 && roi_.h > 0) {
        cv::rectangle(exportImage, 
                     cv::Point(roi_.x, roi_.y), 
                     cv::Point(roi_.x + roi_.w, roi_.y + roi_.h),
                     cv::Scalar(0, 0, 255), // Red in BGR
                     3);
    }

    // Save as TIFF without compression
    if (!cv::imwrite(filePath.toStdString(), exportImage)) {
        QMessageBox::critical(this, tr("Export Error"),
                              tr("Failed to export frame to:\n%1").arg(filePath));
        SPDLOG_ERROR("Failed to write TIFF file: {}", filePath.toStdString());
    } else {
        SPDLOG_INFO("Exported frame {} to TIFF: {}", frame_->index, filePath.toStdString());
        QMessageBox::information(this, tr("Export Complete"),
                                tr("Frame exported successfully to:\n%1").arg(filePath));
    }
}

} // namespace frontend


#include "frontend/adapters/BackgroundCaptureAdapter.h"

#include "backend/AppBackend.h"

#include <QTimer>
#include <opencv2/core.hpp>

namespace frontend {

BackgroundCaptureAdapter::BackgroundCaptureAdapter(backend::AppBackend& backend, QObject* parent)
    : QObject(parent) {
    backend.setBackgroundCaptureCallback([this](const cv::Mat& bg, uint64_t frameIndex) {
        if (bg.empty()) {
            return;
        }
        QImage qimg(bg.data, bg.cols, bg.rows, static_cast<int>(bg.step), QImage::Format_Grayscale8);
        QImage qimgCopy = qimg.copy();
        QTimer::singleShot(0, this, [this, qimgCopy, frameIndex]() {
            emit backgroundAutoCaptured(qimgCopy, frameIndex);
        });
    });
}

} // namespace frontend

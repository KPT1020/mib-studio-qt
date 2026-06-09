#include "frontend/qt/BackgroundFrameQtAdapter.h"

namespace frontend::qt {

QImage toQImage(const backend::BackgroundFrame &frame)
{
    if (frame.empty())
    {
        return {};
    }

    const auto width = static_cast<int>(frame.width);
    const auto height = static_cast<int>(frame.height);
    const auto stride = static_cast<int>(frame.strideBytes);
    const auto *data = frame.data.data();

    switch (frame.pixelFormat)
    {
    case backend::BackgroundFramePixelFormat::Gray8:
        return QImage(data, width, height, stride, QImage::Format_Grayscale8).copy();
    case backend::BackgroundFramePixelFormat::Rgb8:
        return QImage(data, width, height, stride, QImage::Format_RGB888).copy();
    case backend::BackgroundFramePixelFormat::Bgr8:
        return QImage(data, width, height, stride, QImage::Format_BGR888).copy();
    case backend::BackgroundFramePixelFormat::Rgba8:
        return QImage(data, width, height, stride, QImage::Format_RGBA8888).copy();
    case backend::BackgroundFramePixelFormat::Bgra8:
        return QImage(data, width, height, stride, QImage::Format_ARGB32).copy();
    case backend::BackgroundFramePixelFormat::Unknown:
        break;
    }

    return {};
}

} // namespace frontend::qt

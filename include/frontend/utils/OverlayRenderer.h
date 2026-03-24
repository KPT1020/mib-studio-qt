#pragma once

#include <QImage>

namespace cv { class Mat; }
namespace backend::services { struct FilterResult; }

#include "backend/services/ProcessingService.h"

namespace frontend {

enum class OverlayMode {
    None,
    AllContour,
    OuterInnerColorCoded,
    AllMask,
    FilteredMask
};

/** Create processing overlay image from original, mask, optional validation, and mode. */
QImage createProcessingOverlay(const cv::Mat& original,
                               const cv::Mat& mask,
                               const backend::services::FilterResult* validation,
                               OverlayMode mode);

} // namespace frontend

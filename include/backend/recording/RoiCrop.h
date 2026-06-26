// Pure ROI-to-frame clamping for recording crops. No OpenCV/Qt dependency so it
// can be unit tested. Mirrors the clamp the processing path applies before
// cropping (ProcessingService): an empty/invalid ROI means "use the full frame",
// and any ROI is clamped to lie within the frame bounds.
#pragma once

#include <algorithm>

namespace backend::recording {

struct RoiRect {
    int x{0};
    int y{0};
    int w{0};
    int h{0};
};

// Clamp the requested ROI to a frameW x frameH frame. If roiW/roiH <= 0 the
// result is the full frame. The returned rect always satisfies
// 0 <= x, 0 <= y, 1 <= w <= frameW - x, 1 <= h <= frameH - y (assuming a
// non-empty frame).
inline RoiRect clampRoiToFrame(int frameW, int frameH,
                               int roiX, int roiY, int roiW, int roiH)
{
    RoiRect r;
    if (frameW <= 0 || frameH <= 0) return r; // degenerate frame -> empty

    if (roiW <= 0 || roiH <= 0) {
        // No/invalid ROI: full frame.
        return RoiRect{0, 0, frameW, frameH};
    }
    r.x = std::max(0, std::min(roiX, frameW - 1));
    r.y = std::max(0, std::min(roiY, frameH - 1));
    r.w = std::max(1, std::min(roiW, frameW - r.x));
    r.h = std::max(1, std::min(roiH, frameH - r.y));
    return r;
}

} // namespace backend::recording

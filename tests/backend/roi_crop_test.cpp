// roi_crop_test
//
// Pins the ROI-to-frame clamp used to crop recorded frames to the preview ROI.
// An invalid/empty ROI must fall back to the full frame; any ROI must be clamped
// to the frame bounds so the crop can never index out of range.

#include "backend/recording/RoiCrop.h"

#include "support/assert.h"

using backend::recording::clampRoiToFrame;

int main()
{
    // Valid ROI fully inside the frame is preserved.
    {
        const auto r = clampRoiToFrame(640, 480, 100, 80, 200, 150);
        MIB_EXPECT(r.x == 100 && r.y == 80 && r.w == 200 && r.h == 150, "in-bounds ROI preserved");
    }

    // Empty/invalid ROI (w or h <= 0) -> full frame.
    {
        const auto a = clampRoiToFrame(640, 480, 0, 0, 0, 0);
        MIB_EXPECT(a.x == 0 && a.y == 0 && a.w == 640 && a.h == 480, "zero ROI -> full frame");
        const auto b = clampRoiToFrame(640, 480, 10, 10, -5, 100);
        MIB_EXPECT(b.w == 640 && b.h == 480 && b.x == 0 && b.y == 0, "negative w -> full frame");
    }

    // ROI extending past the right/bottom edge is clamped in size.
    {
        const auto r = clampRoiToFrame(640, 480, 600, 460, 200, 200);
        MIB_EXPECT(r.x == 600 && r.y == 460, "origin kept");
        MIB_EXPECT(r.w == 40 && r.h == 20, "size clamped to remaining frame");
    }

    // Negative origin clamps to 0.
    {
        const auto r = clampRoiToFrame(640, 480, -50, -30, 100, 100);
        MIB_EXPECT(r.x == 0 && r.y == 0 && r.w == 100 && r.h == 100, "negative origin -> 0");
    }

    // Origin at/beyond the far edge is pulled in to leave at least 1px.
    {
        const auto r = clampRoiToFrame(640, 480, 1000, 1000, 50, 50);
        MIB_EXPECT(r.x == 639 && r.y == 479 && r.w == 1 && r.h == 1, "far origin clamped to 1px");
    }

    // Degenerate frame -> empty rect (no crop possible).
    {
        const auto r = clampRoiToFrame(0, 0, 0, 0, 10, 10);
        MIB_EXPECT(r.w == 0 && r.h == 0, "degenerate frame -> empty");
    }

    if (mib::test::exitCode() == 0) {
        std::printf("RoiCrop clamp (full-frame fallback + bounds clamp) verified\n");
    }
    return mib::test::exitCode();
}

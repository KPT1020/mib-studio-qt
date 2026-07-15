#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace camera::common {

/**
 * Lightweight container describing a single frame acquired from a camera source.
 *
 * This is kept independent from backend playback structures so that camera
 * implementations can normalize metadata (e.g. line pitch vs. width) before the
 * CaptureService publishes frames to the rest of the application.
 */
struct Frame {
    uint64_t width = 0;
    uint64_t height = 0;
    uint64_t pixelFormat = 0;  // Use PFNC codes to match Euresys metadata.
    size_t linePitch = 0;      // Bytes per line in the buffer (may exceed width).
    uint64_t timestamp = 0;    // Nanoseconds or device ticks depending on source.
    std::vector<uint8_t> data; // Raw image payload copied from the source buffer.
};

} // namespace camera::common



// Pure bounds check for MindVision per-frame geometry vs. the fixed-size
// output buffer allocated once in MindVisionCamera::start(). Extracted so the
// check can be unit tested without the MVCAMSDK or a camera (mirrors
// MindVisionConfig.h's separation of pure validation from SDK-dependent code).
//
// The SDK reports iWidth/iHeight per frame (tSdkFrameHead) while outBuffer_'s
// capacity comes from a separate, earlier CameraGetImageResolution query at
// start(). Nothing otherwise guarantees a later frame report still fits the
// buffer that was sized once at start -- the same class of untrusted-delivery
// mismatch EGrabberCamera::replenishPendingFrames guards against for its own
// buffer/geometry pair.
#pragma once

#include <cstddef>

namespace backend::camera::mindvision {

// True when a frameWidth x frameHeight (1 byte/pixel, MONO8) frame fits
// within a bufferWidth x bufferHeight buffer. Non-positive dimensions on
// either side are rejected: a non-positive frame size is nonsensical, and a
// non-positive buffer means start() never allocated one.
inline bool frameFitsInBuffer(int frameWidth, int frameHeight,
                               int bufferWidth, int bufferHeight)
{
    if (frameWidth <= 0 || frameHeight <= 0 || bufferWidth <= 0 || bufferHeight <= 0)
    {
        return false;
    }

    const std::size_t frameBytes =
        static_cast<std::size_t>(frameWidth) * static_cast<std::size_t>(frameHeight);
    const std::size_t bufferBytes =
        static_cast<std::size_t>(bufferWidth) * static_cast<std::size_t>(bufferHeight);
    return frameBytes <= bufferBytes;
}

} // namespace backend::camera::mindvision

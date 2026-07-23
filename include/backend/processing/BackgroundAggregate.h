#pragma once

// Robust temporal background aggregation for consistent runs.
//
// The realtime auto-background captures a single empty frame, which carries
// that frame's sensor noise and momentary illumination flicker straight into
// the subtraction residual. A per-pixel median over several recent empty
// frames is far more robust (it rejects a stray cell or hot pixel) and, once
// frozen at run start, keeps detection consistent for the whole run.
//
// Pure and Qt-free (OpenCV core only) so it is unit testable without the
// service. Returns empty on invalid/mismatched input so callers can fall back.

#include <vector>

#include <opencv2/core.hpp>

namespace backend::processing {

// Per-pixel median of the given CV_8UC1 frames (all must share one size).
// Returns an empty Mat if the list is empty or any frame is empty / a
// different size / not CV_8UC1.
cv::Mat medianOfFrames(const std::vector<cv::Mat>& frames);

} // namespace backend::processing

#pragma once

// Structure-aware helper for empty-frame detection.
//
// The bundled empty check is a pixel count: a frame is "empty" when fewer than
// empty_frame_pixel_threshold pixels of the thresholded (background-subtracted
// or frame-to-frame) difference are set. A handful of scattered noise pixels
// can clear that bar even with no real object present. Requiring the pixels to
// form a connected blob of a minimum area rejects speckle while still catching
// a genuine cell (a compact component).
//
// Pure and Qt-free (OpenCV core + imgproc only) so it is unit testable and can
// refine the service-level decision without changing the signed processing
// core's ABI-stable isEmpty.

#include <opencv2/core.hpp>

namespace backend::processing {

// Area (in pixels) of the largest 8-connected foreground component in a binary
// mask (CV_8UC1, nonzero = foreground). Returns 0 for an empty/invalid mask or
// when there is no foreground.
int largestComponentArea(const cv::Mat& binaryMask);

} // namespace backend::processing

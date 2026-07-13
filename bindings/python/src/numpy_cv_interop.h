#pragma once

// Minimal, zero-dependency numpy <-> cv::Mat interop for the mib_processing
// pybind11 bindings. Scoped to what the portable pipeline actually needs:
// single-channel 8-bit grayscale 2D images (see docs/gold_standard_metrics.md
// "Portable Processing Contract" -- FrameStore/ProcessingService coerce all
// pipeline images to CV_8UC1). Not a general-purpose OpenCV<->numpy bridge.

#include <opencv2/core.hpp>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>

#include <stdexcept>

namespace mib_processing_bindings {

namespace py = pybind11;

// Converts a 2D uint8 numpy array (H, W) to a CV_8UC1 cv::Mat. Copies the
// data so the returned Mat owns its own memory and is safe to use after the
// Python array is released or mutated.
inline cv::Mat numpyToGrayMat(const py::array& arr) {
    if (arr.ndim() != 2) {
        throw std::invalid_argument("expected a 2D array (H, W) for a grayscale image");
    }
    auto buf = py::array_t<uint8_t, py::array::c_style | py::array::forcecast>(arr);
    const auto rows = static_cast<int>(buf.shape(0));
    const auto cols = static_cast<int>(buf.shape(1));
    cv::Mat mat(rows, cols, CV_8UC1);
    std::memcpy(mat.data, buf.data(), static_cast<size_t>(rows) * static_cast<size_t>(cols));
    return mat;
}

// Converts a CV_8UC1 cv::Mat to a 2D uint8 numpy array (H, W), copying the
// data into a fresh buffer owned by the returned array.
inline py::array_t<uint8_t> matToNumpy(const cv::Mat& mat) {
    const std::vector<py::ssize_t> emptyShape{0, 0};
    if (mat.empty()) {
        return py::array_t<uint8_t>(emptyShape);
    }
    cv::Mat gray = mat;
    if (mat.type() != CV_8UC1) {
        gray.convertTo(gray, CV_8UC1);
    }
    const cv::Mat continuous = gray.isContinuous() ? gray : gray.clone();
    const std::vector<py::ssize_t> shape{continuous.rows, continuous.cols};
    py::array_t<uint8_t> out(shape);
    std::memcpy(out.mutable_data(), continuous.data,
                static_cast<size_t>(continuous.rows) * static_cast<size_t>(continuous.cols));
    return out;
}

}  // namespace mib_processing_bindings

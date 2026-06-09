#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace backend {

enum class BackgroundFramePixelFormat {
    Unknown = 0,
    Gray8,
    Rgb8,
    Bgr8,
    Rgba8,
    Bgra8,
};

struct BackgroundFrame {
    std::uint64_t width{0};
    std::uint64_t height{0};
    std::size_t strideBytes{0};
    BackgroundFramePixelFormat pixelFormat{BackgroundFramePixelFormat::Unknown};
    std::vector<std::uint8_t> data;

    bool empty() const { return width == 0 || height == 0 || data.empty(); }
};

struct BackgroundCaptureEvent {
    BackgroundFrame frame;
    std::uint64_t frameIndex{0};
};

} // namespace backend

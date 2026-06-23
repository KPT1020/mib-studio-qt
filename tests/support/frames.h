// Synthetic frame generators + directory writer for mock-camera-driven tests.
#pragma once

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>

namespace mib::test {

// Ring/blob frame guaranteed to yield a non-empty mask under a lenient config.
inline cv::Mat ringFrame(int w, int h, uint64_t i)
{
    cv::Mat m(h, w, CV_8UC1, cv::Scalar(0));
    const int cx = w / 3 + static_cast<int>(i % static_cast<uint64_t>(std::max(1, w / 4)));
    const int r = std::min(w, h) / 5;
    cv::circle(m, cv::Point(cx, h / 2), r, cv::Scalar(220), -1);
    cv::circle(m, cv::Point(cx, h / 2), r / 2, cv::Scalar(0), -1);
    return m;
}

inline bool writeFrames(const std::filesystem::path& dir, int count, int w = 256, int h = 256)
{
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    for (int i = 0; i < count; ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "frame_%05d.png", i);
        if (!cv::imwrite((dir / name).string(), ringFrame(w, h, static_cast<uint64_t>(i)))) {
            return false;
        }
    }
    return true;
}

} // namespace mib::test

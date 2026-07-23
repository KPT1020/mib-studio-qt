#include "backend/processing/BackgroundAggregate.h"

#include <algorithm>

namespace backend::processing {

cv::Mat medianOfFrames(const std::vector<cv::Mat>& frames) {
    if (frames.empty()) {
        return {};
    }
    const cv::Size size = frames.front().size();
    for (const cv::Mat& frame : frames) {
        if (frame.empty() || frame.type() != CV_8UC1 || frame.size() != size) {
            return {};
        }
    }

    const int n = static_cast<int>(frames.size());
    if (n == 1) {
        return frames.front().clone();
    }

    cv::Mat out(size, CV_8UC1);
    std::vector<unsigned char> column(static_cast<size_t>(n));
    std::vector<const unsigned char*> rowPtrs(static_cast<size_t>(n));
    const size_t mid = static_cast<size_t>(n) / 2;

    for (int y = 0; y < size.height; ++y) {
        for (int k = 0; k < n; ++k) {
            rowPtrs[static_cast<size_t>(k)] = frames[static_cast<size_t>(k)].ptr<unsigned char>(y);
        }
        unsigned char* outRow = out.ptr<unsigned char>(y);
        for (int x = 0; x < size.width; ++x) {
            for (int k = 0; k < n; ++k) {
                column[static_cast<size_t>(k)] = rowPtrs[static_cast<size_t>(k)][x];
            }
            std::nth_element(column.begin(), column.begin() + mid, column.end());
            outRow[x] = column[mid]; // upper median for even n; fine for background
        }
    }
    return out;
}

} // namespace backend::processing

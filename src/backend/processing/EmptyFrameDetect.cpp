#include "backend/processing/EmptyFrameDetect.h"

#include <opencv2/imgproc.hpp>

namespace backend::processing {

int largestComponentArea(const cv::Mat& binaryMask) {
    if (binaryMask.empty() || binaryMask.type() != CV_8UC1) {
        return 0;
    }
    cv::Mat labels;
    cv::Mat stats;
    cv::Mat centroids;
    const int count =
        cv::connectedComponentsWithStats(binaryMask, labels, stats, centroids, 8, CV_32S);
    int best = 0;
    // Label 0 is the background; scan real components only.
    for (int i = 1; i < count; ++i) {
        const int area = stats.at<int>(i, cv::CC_STAT_AREA);
        if (area > best) {
            best = area;
        }
    }
    return best;
}

} // namespace backend::processing

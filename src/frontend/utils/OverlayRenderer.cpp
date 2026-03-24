#include "frontend/utils/OverlayRenderer.h"
#include "backend/services/ProcessingService.h"

#include <QImage>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

namespace frontend {

namespace {

static QImage matToQImage(const cv::Mat& mat) {
    if (mat.empty()) {
        return QImage();
    }
    if (mat.type() == CV_8UC1) {
        QImage img(mat.data, mat.cols, mat.rows, static_cast<int>(mat.step), QImage::Format_Grayscale8);
        return img.copy();
    }
    if (mat.type() == CV_8UC3) {
        cv::Mat rgb;
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
        QImage img(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888);
        return img.copy();
    }
    if (mat.type() == CV_8UC4) {
        cv::Mat rgba;
        cv::cvtColor(mat, rgba, cv::COLOR_BGRA2RGBA);
        QImage img(rgba.data, rgba.cols, rgba.rows, static_cast<int>(rgba.step), QImage::Format_RGBA8888);
        return img.copy();
    }
    cv::Mat gray;
    cv::cvtColor(mat, gray, cv::COLOR_BGR2GRAY);
    QImage img(gray.data, gray.cols, gray.rows, static_cast<int>(gray.step), QImage::Format_Grayscale8);
    return img.copy();
}

static cv::Mat toRgb(const cv::Mat& original) {
    if (original.empty()) return cv::Mat();
    cv::Mat rgb;
    if (original.channels() == 1) {
        cv::cvtColor(original, rgb, cv::COLOR_GRAY2RGB);
    } else {
        rgb = original.clone();
        if (rgb.channels() == 3) {
            cv::cvtColor(rgb, rgb, cv::COLOR_BGR2RGB);
        }
    }
    return rgb;
}

/** Apply green tint to overlay where mask is non-zero (in-place on rgb). */
static void applyMaskTint(cv::Mat& rgb, const cv::Mat& mask) {
    if (rgb.empty() || mask.empty()) return;
    for (int y = 0; y < rgb.rows && y < mask.rows; ++y) {
        for (int x = 0; x < rgb.cols && x < mask.cols; ++x) {
            if (mask.at<uchar>(y, x) > 0) {
                cv::Vec3b& pixel = rgb.at<cv::Vec3b>(y, x);
                pixel[0] = static_cast<uchar>(pixel[0] * 0.7);
                pixel[1] = static_cast<uchar>(std::min(255.0, pixel[1] * 0.7 + 255.0 * 0.3));
                pixel[2] = static_cast<uchar>(pixel[2] * 0.7);
            }
        }
    }
}

} // namespace

QImage createProcessingOverlay(const cv::Mat& original,
                               const cv::Mat& mask,
                               const backend::services::FilterResult* validation,
                               OverlayMode mode) {
    if (original.empty()) {
        return QImage();
    }
    if (mode == OverlayMode::None) {
        return matToQImage(original);
    }
    cv::Mat rgb = toRgb(original);
    if (rgb.empty()) return QImage();

    if (mode == OverlayMode::AllMask) {
        if (mask.empty()) return matToQImage(original);
        applyMaskTint(rgb, mask);
        QImage img(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888);
        return img.copy();
    }

    if (mask.empty()) {
        return matToQImage(original);
    }

    std::vector<std::vector<cv::Point>> contours;
    cv::Mat hierarchyMat;
    cv::findContours(mask.clone(), contours, hierarchyMat, cv::RETR_CCOMP, cv::CHAIN_APPROX_SIMPLE);

    const int n = static_cast<int>(contours.size());
    if (n == 0) {
        return matToQImage(original);
    }

    // hierarchy: [next, prev, first_child, parent] per contour
    const bool hasHierarchy = !hierarchyMat.empty() && hierarchyMat.rows == 1 && hierarchyMat.cols == n;

    if (mode == OverlayMode::AllContour) {
        cv::drawContours(rgb, contours, -1, cv::Scalar(0, 255, 0), 2);
        QImage img(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888);
        return img.copy();
    }

    if (mode == OverlayMode::OuterInnerColorCoded && hasHierarchy) {
        const bool valid = validation && validation->isValid;
        const cv::Scalar outerColor = valid ? cv::Scalar(0, 165, 255)   : cv::Scalar(0, 0, 255);   // orange / red
        const cv::Scalar innerColor = valid ? cv::Scalar(255, 255, 0)   : cv::Scalar(255, 0, 255); // cyan / magenta
        for (int i = 0; i < n; ++i) {
            const int parent = hierarchyMat.at<cv::Vec4i>(0, i)[3];
            if (parent < 0) {
                cv::drawContours(rgb, contours, i, outerColor, 2);
            } else {
                cv::drawContours(rgb, contours, i, innerColor, 2);
            }
        }
        QImage img(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888);
        return img.copy();
    }

    if (mode == OverlayMode::FilteredMask && hasHierarchy) {
        // Find outer contour that has exactly one inner child (accepted pair)
        int acceptedOuter = -1;
        for (int i = 0; i < n; ++i) {
            const int parent = hierarchyMat.at<cv::Vec4i>(0, i)[3];
            if (parent >= 0) continue;
            const int firstChild = hierarchyMat.at<cv::Vec4i>(0, i)[2];
            if (firstChild < 0) continue;
            int childCount = 0;
            for (int c = firstChild; c >= 0; c = hierarchyMat.at<cv::Vec4i>(0, c)[0]) {
                ++childCount;
            }
            if (validation && validation->hasSingleInnerContour) {
                if (childCount == 1) {
                    acceptedOuter = i;
                    break;
                }
            } else {
                if (childCount >= 1) {
                    acceptedOuter = i;
                    break;
                }
            }
        }
        if (acceptedOuter >= 0) {
            cv::Mat filteredMask = cv::Mat::zeros(mask.rows, mask.cols, CV_8UC1);
            cv::drawContours(filteredMask, contours, acceptedOuter, cv::Scalar(255), -1);
            const int firstChild = hierarchyMat.at<cv::Vec4i>(0, acceptedOuter)[2];
            if (firstChild >= 0) {
                for (int c = firstChild; c >= 0; c = hierarchyMat.at<cv::Vec4i>(0, c)[0]) {
                    cv::drawContours(filteredMask, contours, c, cv::Scalar(0), -1);
                }
            }
            applyMaskTint(rgb, filteredMask);
        }
        QImage img(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888);
        return img.copy();
    }

    return matToQImage(original);
}

} // namespace frontend

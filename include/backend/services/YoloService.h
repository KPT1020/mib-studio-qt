#pragma once

#include <mutex>
#include <memory>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

// Forward declarations to avoid including ONNX Runtime headers in header
namespace Ort {
    class Env;
    class Session;
}

namespace backend::services {

class YoloService {
public:
    YoloService();
    ~YoloService();

    // Initialize the service by loading the ONNX model from the given path
    // Returns true if successful, false otherwise
    bool initialize(const std::string& modelPath);

    // Check if the model is loaded and ready
    bool isLoaded() const { return isLoaded_; }

    // Run segmentation inference on a grayscale image.
    // Returns true when a binary mask (CV_8UC1, 0/255) is produced.
    bool inferSegmentationMask(const cv::Mat& grayInput, cv::Mat& outMask, float threshold = 0.5f) const;

    // Get the ONNX Runtime session (returns nullptr if not loaded)
    Ort::Session* getSession() const;

    // Get the ONNX Runtime environment (returns nullptr if not initialized)
    Ort::Env* getEnv() const;

private:
    bool isLoaded_{false};
    std::unique_ptr<Ort::Env> env_;
    std::unique_ptr<Ort::Session> session_;
    std::string modelPath_;
    std::string inputName_;
    std::string outputName_;
    std::vector<int64_t> inputShape_;
    std::vector<int64_t> outputShape_;
    mutable std::mutex inferenceMutex_;

    // Helper to resolve model path (similar to isoelastic curve loading)
    static std::string resolveModelPath(const std::string& basePath);
};

} // namespace backend::services

#pragma once

#include <memory>
#include <string>

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

    // Get the ONNX Runtime session (returns nullptr if not loaded)
    Ort::Session* getSession() const;

    // Get the ONNX Runtime environment (returns nullptr if not initialized)
    Ort::Env* getEnv() const;

private:
    bool isLoaded_{false};
    std::unique_ptr<Ort::Env> env_;
    std::unique_ptr<Ort::Session> session_;
    std::string modelPath_;

    // Helper to resolve model path (similar to isoelastic curve loading)
    static std::string resolveModelPath(const std::string& basePath);
};

} // namespace backend::services

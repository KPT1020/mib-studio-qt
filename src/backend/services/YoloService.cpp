#include "backend/services/YoloService.h"

#include <spdlog/spdlog.h>
#include <onnxruntime_cxx_api.h>

#include <filesystem>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#include <stringapiset.h>
#endif

namespace backend::services {

YoloService::YoloService() = default;

YoloService::~YoloService() {
    session_.reset();
    env_.reset();
}

std::string YoloService::resolveModelPath(const std::string& basePath) {
    // Try base path first
    if (std::filesystem::exists(basePath)) {
        return basePath;
    }

    // If basePath is already a full path but doesn't exist, try alternative locations
    std::filesystem::path base(basePath);
    
    // If basePath contains "yolo11n-seg.onnx", extract the directory and try alternatives
    if (base.filename() == "yolo11n-seg.onnx") {
        std::filesystem::path baseDir = base.parent_path();
        
        // Try ../resources/models/ (if baseDir was resources/models but file not there)
        std::filesystem::path altPath = baseDir.parent_path().parent_path() / "resources" / "models" / "yolo11n-seg.onnx";
        if (std::filesystem::exists(altPath)) {
            return altPath.string();
        }
        
        // Try ../../resources/models/ (development from build directory)
        altPath = baseDir.parent_path().parent_path().parent_path() / "resources" / "models" / "yolo11n-seg.onnx";
        if (std::filesystem::exists(altPath)) {
            return altPath.string();
        }
    }

    // Try current working directory (development)
    std::filesystem::path cwd = std::filesystem::current_path();
    std::filesystem::path modelPath = cwd / "resources" / "models" / "yolo11n-seg.onnx";
    if (std::filesystem::exists(modelPath)) {
        return modelPath.string();
    }

    // Try ../resources/models/ from current directory
    modelPath = cwd.parent_path() / "resources" / "models" / "yolo11n-seg.onnx";
    if (std::filesystem::exists(modelPath)) {
        return modelPath.string();
    }

    // Return original path if nothing found (will fail later with better error message)
    return basePath;
}

bool YoloService::initialize(const std::string& modelPath) {
    if (isLoaded_) {
        SPDLOG_WARN("YoloService: Model already loaded, skipping initialization");
        return true;
    }

    // Resolve model path
    std::string resolvedPath = resolveModelPath(modelPath);
    
    if (!std::filesystem::exists(resolvedPath)) {
        SPDLOG_WARN("YoloService: Model file not found at: {}", resolvedPath);
        SPDLOG_WARN("YoloService: Tried paths relative to executable and development paths");
        return false;
    }

    try {
        // Initialize ONNX Runtime environment
        env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "YoloService");
        
        // Create session options
        Ort::SessionOptions sessionOptions;
        sessionOptions.SetIntraOpNumThreads(1);
        sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        // Create session
        // On Windows, ONNX Runtime requires wide string (wchar_t*) for the model path
#ifdef _WIN32
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, resolvedPath.c_str(), -1, NULL, 0);
        std::wstring wpath(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, resolvedPath.c_str(), -1, &wpath[0], size_needed);
        session_ = std::make_unique<Ort::Session>(*env_, wpath.c_str(), sessionOptions);
#else
        session_ = std::make_unique<Ort::Session>(*env_, resolvedPath.c_str(), sessionOptions);
#endif

        modelPath_ = resolvedPath;
        isLoaded_ = true;

        SPDLOG_INFO("YoloService: Successfully loaded YOLO 11 seg nano model from: {}", resolvedPath);
        return true;
    }
    catch (const Ort::Exception& e) {
        SPDLOG_ERROR("YoloService: ONNX Runtime error loading model: {}", e.what());
        return false;
    }
    catch (const std::exception& e) {
        SPDLOG_ERROR("YoloService: Error loading model: {}", e.what());
        return false;
    }
    catch (...) {
        SPDLOG_ERROR("YoloService: Unknown error loading model");
        return false;
    }
}

Ort::Session* YoloService::getSession() const {
    return session_.get();
}

Ort::Env* YoloService::getEnv() const {
    return env_.get();
}

} // namespace backend::services

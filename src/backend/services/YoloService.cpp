#include "backend/services/YoloService.h"

#include <spdlog/spdlog.h>
#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>

#ifdef _WIN32
#include <windows.h>
#include <stringapiset.h>
#endif

namespace backend::services {

namespace {

constexpr float kEpsilon = 1e-6f;

bool isProbabilityRange(const float minValue, const float maxValue) {
    return minValue >= -kEpsilon && maxValue <= 1.0f + kEpsilon;
}

inline float sigmoid(const float x) {
    if (x >= 0.0f) {
        const float z = std::exp(-x);
        return 1.0f / (1.0f + z);
    }
    const float z = std::exp(x);
    return z / (1.0f + z);
}

} // namespace

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

        if (session_->GetInputCount() == 0 || session_->GetOutputCount() == 0) {
            SPDLOG_ERROR("YoloService: Model has no inputs or outputs");
            session_.reset();
            env_.reset();
            return false;
        }

        Ort::AllocatorWithDefaultOptions allocator;
        char* inputName = session_->GetInputName(0, allocator);
        char* outputName = session_->GetOutputName(0, allocator);
        if (inputName == nullptr || outputName == nullptr) {
            if (inputName != nullptr) allocator.Free(inputName);
            if (outputName != nullptr) allocator.Free(outputName);
            SPDLOG_ERROR("YoloService: Failed to read model input/output names");
            session_.reset();
            env_.reset();
            return false;
        }

        inputName_ = inputName;
        outputName_ = outputName;
        allocator.Free(inputName);
        allocator.Free(outputName);

        const auto inputTypeInfo = session_->GetInputTypeInfo(0);
        const auto outputTypeInfo = session_->GetOutputTypeInfo(0);
        inputShape_ = inputTypeInfo.GetTensorTypeAndShapeInfo().GetShape();
        outputShape_ = outputTypeInfo.GetTensorTypeAndShapeInfo().GetShape();

        modelPath_ = resolvedPath;
        isLoaded_ = true;

        SPDLOG_INFO("YoloService: Successfully loaded segmentation model from: {}", resolvedPath);
        SPDLOG_INFO("YoloService: input='{}' shape=[{}{}{}{}], output='{}' shape=[{}{}{}{}]",
                    inputName_,
                    inputShape_.size() > 0 ? std::to_string(inputShape_[0]) : "?",
                    inputShape_.size() > 1 ? "," + std::to_string(inputShape_[1]) : "",
                    inputShape_.size() > 2 ? "," + std::to_string(inputShape_[2]) : "",
                    inputShape_.size() > 3 ? "," + std::to_string(inputShape_[3]) : "",
                    outputName_,
                    outputShape_.size() > 0 ? std::to_string(outputShape_[0]) : "?",
                    outputShape_.size() > 1 ? "," + std::to_string(outputShape_[1]) : "",
                    outputShape_.size() > 2 ? "," + std::to_string(outputShape_[2]) : "",
                    outputShape_.size() > 3 ? "," + std::to_string(outputShape_[3]) : "");
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

bool YoloService::inferSegmentationMask(const cv::Mat& grayInput, cv::Mat& outMask, float threshold) const {
    outMask.release();
    if (!isLoaded_ || session_ == nullptr) {
        return false;
    }
    if (grayInput.empty()) {
        return false;
    }

    cv::Mat gray;
    if (grayInput.type() == CV_8UC1) {
        gray = grayInput;
    } else if (grayInput.channels() == 3) {
        cv::cvtColor(grayInput, gray, cv::COLOR_BGR2GRAY);
    } else {
        grayInput.convertTo(gray, CV_8UC1);
    }

    if (gray.empty()) {
        return false;
    }

    threshold = std::max(0.0f, std::min(1.0f, threshold));

    try {
        std::scoped_lock lk(inferenceMutex_);

        const bool isNhwcInput = inputShape_.size() == 4 && inputShape_[3] > 0 && inputShape_[1] != 1;
        int64_t inputChannels = 1;
        int64_t inputHeight = gray.rows;
        int64_t inputWidth = gray.cols;
        if (inputShape_.size() == 4) {
            if (isNhwcInput) {
                inputHeight = inputShape_[1] > 0 ? inputShape_[1] : inputHeight;
                inputWidth = inputShape_[2] > 0 ? inputShape_[2] : inputWidth;
                inputChannels = inputShape_[3] > 0 ? inputShape_[3] : 1;
            } else {
                inputChannels = inputShape_[1] > 0 ? inputShape_[1] : 1;
                inputHeight = inputShape_[2] > 0 ? inputShape_[2] : inputHeight;
                inputWidth = inputShape_[3] > 0 ? inputShape_[3] : inputWidth;
            }
        }

        if (inputHeight <= 0 || inputWidth <= 0) {
            inputHeight = gray.rows;
            inputWidth = gray.cols;
        }
        if (inputChannels <= 0) {
            inputChannels = 1;
        }
        if (inputChannels != 1 && inputChannels != 3) {
            SPDLOG_WARN("YoloService: unsupported input channel count {}", inputChannels);
            return false;
        }

        cv::Mat resized;
        if (gray.rows != inputHeight || gray.cols != inputWidth) {
            cv::resize(gray, resized, cv::Size(static_cast<int>(inputWidth), static_cast<int>(inputHeight)), 0, 0, cv::INTER_LINEAR);
        } else {
            resized = gray;
        }

        const size_t pixelCount = static_cast<size_t>(inputHeight * inputWidth);
        const size_t tensorSize = pixelCount * static_cast<size_t>(inputChannels);
        std::vector<float> inputTensor(tensorSize, 0.0f);

        if (isNhwcInput) {
            for (int64_t y = 0; y < inputHeight; ++y) {
                const uint8_t* row = resized.ptr<uint8_t>(static_cast<int>(y));
                for (int64_t x = 0; x < inputWidth; ++x) {
                    const float value = static_cast<float>(row[x]) / 255.0f;
                    for (int64_t c = 0; c < inputChannels; ++c) {
                        const size_t idx = static_cast<size_t>((y * inputWidth + x) * inputChannels + c);
                        inputTensor[idx] = value;
                    }
                }
            }
        } else {
            for (int64_t c = 0; c < inputChannels; ++c) {
                const size_t channelOffset = static_cast<size_t>(c * inputHeight * inputWidth);
                for (int64_t y = 0; y < inputHeight; ++y) {
                    const uint8_t* row = resized.ptr<uint8_t>(static_cast<int>(y));
                    for (int64_t x = 0; x < inputWidth; ++x) {
                        inputTensor[channelOffset + static_cast<size_t>(y * inputWidth + x)] =
                            static_cast<float>(row[x]) / 255.0f;
                    }
                }
            }
        }

        std::vector<int64_t> runtimeInputShape = inputShape_;
        if (runtimeInputShape.size() != 4) {
            runtimeInputShape = {1, inputChannels, inputHeight, inputWidth};
        } else if (isNhwcInput) {
            runtimeInputShape[0] = 1;
            runtimeInputShape[1] = inputHeight;
            runtimeInputShape[2] = inputWidth;
            runtimeInputShape[3] = inputChannels;
        } else {
            runtimeInputShape[0] = 1;
            runtimeInputShape[1] = inputChannels;
            runtimeInputShape[2] = inputHeight;
            runtimeInputShape[3] = inputWidth;
        }

        Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value inputValue = Ort::Value::CreateTensor<float>(
            memoryInfo, inputTensor.data(), inputTensor.size(), runtimeInputShape.data(), runtimeInputShape.size());

        const char* inputNames[] = {inputName_.c_str()};
        const char* outputNames[] = {outputName_.c_str()};
        auto outputValues = session_->Run(Ort::RunOptions{nullptr},
                                          inputNames, &inputValue, 1,
                                          outputNames, 1);
        if (outputValues.empty() || !outputValues[0].IsTensor()) {
            return false;
        }

        auto outputTypeInfo = outputValues[0].GetTensorTypeAndShapeInfo();
        auto outShape = outputTypeInfo.GetShape();
        float* outData = outputValues[0].GetTensorMutableData<float>();
        if (outData == nullptr) {
            return false;
        }

        cv::Mat modelMask;
        if (outShape.size() == 4 && outShape[0] == 1 && outShape[1] >= 1) {
            const int64_t classes = outShape[1];
            const int64_t outH = outShape[2];
            const int64_t outW = outShape[3];
            if (outH <= 0 || outW <= 0) return false;
            modelMask = cv::Mat(static_cast<int>(outH), static_cast<int>(outW), CV_8UC1, cv::Scalar(0));
            if (classes == 1) {
                const size_t planeSize = static_cast<size_t>(outH * outW);
                const auto [minIt, maxIt] = std::minmax_element(outData, outData + planeSize);
                const bool alreadyProb = isProbabilityRange(*minIt, *maxIt);
                for (int64_t y = 0; y < outH; ++y) {
                    uint8_t* row = modelMask.ptr<uint8_t>(static_cast<int>(y));
                    for (int64_t x = 0; x < outW; ++x) {
                        const size_t idx = static_cast<size_t>(y * outW + x);
                        const float value = alreadyProb ? outData[idx] : sigmoid(outData[idx]);
                        row[x] = (value >= threshold) ? 255 : 0;
                    }
                }
            } else {
                const size_t planeSize = static_cast<size_t>(outH * outW);
                for (int64_t y = 0; y < outH; ++y) {
                    uint8_t* row = modelMask.ptr<uint8_t>(static_cast<int>(y));
                    for (int64_t x = 0; x < outW; ++x) {
                        const size_t pixelOffset = static_cast<size_t>(y * outW + x);
                        float best = -std::numeric_limits<float>::infinity();
                        int64_t bestClass = 0;
                        for (int64_t c = 0; c < classes; ++c) {
                            const float score = outData[static_cast<size_t>(c) * planeSize + pixelOffset];
                            if (score > best) {
                                best = score;
                                bestClass = c;
                            }
                        }
                        row[x] = (bestClass == 0) ? 0 : 255;
                    }
                }
            }
        } else if (outShape.size() == 4 && outShape[0] == 1 && outShape[3] >= 1) {
            // NHWC output
            const int64_t outH = outShape[1];
            const int64_t outW = outShape[2];
            const int64_t classes = outShape[3];
            if (outH <= 0 || outW <= 0 || classes <= 0) return false;
            modelMask = cv::Mat(static_cast<int>(outH), static_cast<int>(outW), CV_8UC1, cv::Scalar(0));
            if (classes == 1) {
                const size_t total = static_cast<size_t>(outH * outW * classes);
                const auto [minIt, maxIt] = std::minmax_element(outData, outData + total);
                const bool alreadyProb = isProbabilityRange(*minIt, *maxIt);
                for (int64_t y = 0; y < outH; ++y) {
                    uint8_t* row = modelMask.ptr<uint8_t>(static_cast<int>(y));
                    for (int64_t x = 0; x < outW; ++x) {
                        const size_t idx = static_cast<size_t>((y * outW + x) * classes);
                        const float value = alreadyProb ? outData[idx] : sigmoid(outData[idx]);
                        row[x] = (value >= threshold) ? 255 : 0;
                    }
                }
            } else {
                for (int64_t y = 0; y < outH; ++y) {
                    uint8_t* row = modelMask.ptr<uint8_t>(static_cast<int>(y));
                    for (int64_t x = 0; x < outW; ++x) {
                        const size_t base = static_cast<size_t>((y * outW + x) * classes);
                        float best = -std::numeric_limits<float>::infinity();
                        int64_t bestClass = 0;
                        for (int64_t c = 0; c < classes; ++c) {
                            const float score = outData[base + static_cast<size_t>(c)];
                            if (score > best) {
                                best = score;
                                bestClass = c;
                            }
                        }
                        row[x] = (bestClass == 0) ? 0 : 255;
                    }
                }
            }
        } else if (outShape.size() == 3 && outShape[0] == 1) {
            const int64_t outH = outShape[1];
            const int64_t outW = outShape[2];
            if (outH <= 0 || outW <= 0) return false;
            modelMask = cv::Mat(static_cast<int>(outH), static_cast<int>(outW), CV_8UC1, cv::Scalar(0));
            const size_t planeSize = static_cast<size_t>(outH * outW);
            const auto [minIt, maxIt] = std::minmax_element(outData, outData + planeSize);
            const bool alreadyProb = isProbabilityRange(*minIt, *maxIt);
            for (int64_t y = 0; y < outH; ++y) {
                uint8_t* row = modelMask.ptr<uint8_t>(static_cast<int>(y));
                for (int64_t x = 0; x < outW; ++x) {
                    const size_t idx = static_cast<size_t>(y * outW + x);
                    const float value = alreadyProb ? outData[idx] : sigmoid(outData[idx]);
                    row[x] = (value >= threshold) ? 255 : 0;
                }
            }
        } else {
            SPDLOG_WARN("YoloService: unsupported output shape rank={} for segmentation inference", outShape.size());
            return false;
        }

        if (modelMask.empty()) {
            return false;
        }

        if (modelMask.size() != gray.size()) {
            cv::resize(modelMask, outMask, gray.size(), 0, 0, cv::INTER_NEAREST);
        } else {
            outMask = modelMask;
        }
        return !outMask.empty();
    } catch (const Ort::Exception& e) {
        SPDLOG_WARN("YoloService: inference failed (ONNX Runtime): {}", e.what());
        return false;
    } catch (const std::exception& e) {
        SPDLOG_WARN("YoloService: inference failed: {}", e.what());
        return false;
    } catch (...) {
        SPDLOG_WARN("YoloService: inference failed with unknown error");
        return false;
    }
}

} // namespace backend::services

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <opencv2/core.hpp>

namespace backend::processing {

struct ProcessingCoreIdentity {
    std::string version{"0.1.0"};
    uint32_t contractVersion{1};
    uint32_t engineAbiVersion{1};
    std::string artifactSha256;
    std::string releaseTag;
    std::string manifestSha256;
    std::string source{"bundled"};
    std::string buildId;
    std::string runtimeFingerprint;

    bool operator==(const ProcessingCoreIdentity& other) const;
    bool operator!=(const ProcessingCoreIdentity& other) const { return !(*this == other); }
};

struct KernelConfig {
    int gaussianBlurSize{3};
    int backgroundSubtractThreshold{8};
    int morphologyKernelSize{3};
    int morphologyIterations{1};
    int emptyFramePixelThreshold{100};
    bool absoluteBackgroundDifference{false};
};

struct KernelRoi {
    int x{0};
    int y{0};
    int width{0};
    int height{0};
};

class IProcessingKernel {
public:
    virtual ~IProcessingKernel() = default;

    virtual const ProcessingCoreIdentity& identity() const noexcept = 0;
    virtual bool processMask(const cv::Mat& gray,
                             const cv::Mat& background,
                             const KernelConfig& config,
                             const KernelRoi& roi,
                             cv::Mat& outputMask,
                             std::string* error = nullptr) = 0;
    virtual bool isEmpty(const cv::Mat& gray,
                         const cv::Mat& background,
                         const KernelConfig& config,
                         const KernelRoi& roi,
                         bool& outputIsEmpty,
                         std::string* error = nullptr) = 0;
    virtual bool reset(std::string* error = nullptr) = 0;
};

std::shared_ptr<IProcessingKernel> makeBundledProcessingKernel();
ProcessingCoreIdentity bundledProcessingCoreIdentity();

} // namespace backend::processing

#pragma once

#include "camera/common/ICamera.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace camera::mock {

bool isSupportedMockImageFile(const std::filesystem::path& path);
bool validateMockImageFolder(const std::filesystem::path& folder, std::string& errorMessage, bool requireAllReadable = true);

struct MockCameraOptions {
    std::filesystem::path folder;
    std::chrono::microseconds frameInterval{33'000};
    bool loopFiles{true};
};

class MockCamera : public camera::common::ICamera {
public:
    explicit MockCamera(MockCameraOptions options);
    ~MockCamera() override = default;

    void applyConfig(const camera::common::CameraConfig& config) override;
    bool start() override;
    void stop() override;
    bool isRunning() const override { return running_; }

    bool grabFrame(camera::common::Frame& out) override;
    bool pollStats(camera::common::CameraStats& out) const override;

    void setFrameInterval(std::chrono::microseconds interval);
    void setLooping(bool loop);

private:
    void refreshFileList();
    bool loadFrameFromPath(const std::filesystem::path& path, camera::common::Frame& frame);
    bool preloadFrames();

    MockCameraOptions options_;
    camera::common::CameraConfig config_{};
    std::vector<std::filesystem::path> files_;
    std::vector<camera::common::Frame> preloadedFrames_;
    size_t nextIndex_{0};

    bool running_{false};
    std::chrono::steady_clock::time_point lastFrameTime_{};
    mutable camera::common::CameraStats stats_{};
    std::string lastError_;
};

} // namespace camera::mock



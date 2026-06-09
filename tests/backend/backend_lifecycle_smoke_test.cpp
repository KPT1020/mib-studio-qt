#include "backend/app/AppBackend.h"
#include "backend/camera/mock/MockCamera.h"

#include <QCoreApplication>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

namespace
{
std::filesystem::path makeTempDir()
{
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<unsigned long long> dist;
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        const auto path = std::filesystem::temp_directory_path() /
                          ("mib_backend_lifecycle_" + std::to_string(dist(gen)));
        std::error_code ec;
        if (std::filesystem::create_directories(path, ec))
        {
            return path;
        }
    }
    throw std::runtime_error("failed to create temporary directory");
}

void setEnv(const char* name, const char* value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    const auto dataDir = makeTempDir();
    const auto mockDir = dataDir / "mock_frames";
    std::filesystem::create_directories(mockDir);

    setEnv("MIB_CAMERA_MODE", "mock");
    setEnv("MIB_MOCK_CAMERA_DIR", mockDir.string().c_str());
    setEnv("MIB_MOCK_CAMERA_INTERVAL_MS", "1");

    backend::AppBackend backend;
    bool callbackTouched = false;
    backend.setBackgroundCaptureCallback([&callbackTouched](const backend::BackgroundCaptureEvent&) {
        callbackTouched = true;
    });
    backend.setBackgroundCaptureCallback({});
    if (callbackTouched)
    {
        std::cerr << "AppBackend background callback should not run during callback registration\n";
        std::filesystem::remove_all(dataDir);
        return 1;
    }

    if (!backend.initialize(dataDir.string()))
    {
        std::cerr << "AppBackend initialize should succeed in hardware-free mock mode\n";
        std::filesystem::remove_all(dataDir);
        return 2;
    }

    if (!backend.getFrameStore())
    {
        std::cerr << "AppBackend should create a frame store during initialize\n";
        std::filesystem::remove_all(dataDir);
        return 3;
    }

    if (!backend.isCameraConfigured())
    {
        std::cerr << "AppBackend should configure a mock camera when hardware SDKs are disabled\n";
        std::filesystem::remove_all(dataDir);
        return 4;
    }

    if (backend.isFrameRecording() || backend.frameRecordingCount() != 0 ||
        backend.frameRecordingFiltered() != 0)
    {
        std::cerr << "AppBackend recording lifecycle should start idle\n";
        std::filesystem::remove_all(dataDir);
        return 5;
    }

    backend.setLastConfigJson("{\"smoke\":true}");
    if (backend.getLastConfigJson() != "{\"smoke\":true}")
    {
        std::cerr << "AppBackend config JSON should round-trip\n";
        std::filesystem::remove_all(dataDir);
        return 6;
    }

    camera::mock::MockCameraOptions options;
    options.folder = mockDir;
    backend.configureMockCamera(options);
    if (!backend.isCameraConfigured())
    {
        std::cerr << "configureMockCamera should leave the backend camera-configured\n";
        std::filesystem::remove_all(dataDir);
        return 7;
    }

    backend.stopFrameRecording();
    std::filesystem::remove_all(dataDir);
    return 0;
}

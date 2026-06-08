#include "backend/AppBackend.h"

#include <QCoreApplication>

#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>

namespace
{
std::string makeTempDir()
{
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<unsigned long long> dist;
    auto path = std::filesystem::temp_directory_path() /
                ("mib_backend_lifecycle_" + std::to_string(dist(gen)));
    std::filesystem::create_directories(path);
    return path.string();
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

    setEnv("MIB_CAMERA_MODE", "mock");
    setEnv("MIB_DISABLED_SERVICES", "autofocus,trigger,yolo");

    const std::string dataDir = makeTempDir();
    backend::AppBackend backend;
    if (!backend.initialize(dataDir))
    {
        return 1;
    }
    if (!backend.isCameraConfigured())
    {
        return 2;
    }

    std::error_code ec;
    std::filesystem::remove_all(dataDir, ec);
    return 0;
}

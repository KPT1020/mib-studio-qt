#include "backend/services/Hdf5Service.h"

#include <filesystem>
#include <random>
#include <string>

namespace
{
std::string makeTempPath()
{
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<unsigned long long> dist;
    const auto suffix = dist(gen);
    return (std::filesystem::temp_directory_path() /
            ("mib_backend_smoke_" + std::to_string(suffix) + ".h5"))
        .string();
}
} // namespace

int main()
{
    backend::services::Hdf5Service hdf5;
    if (!hdf5.initialize(std::filesystem::temp_directory_path().string()))
    {
        return 1;
    }

    const std::string path = makeTempPath();
    if (!hdf5.openFile(path))
    {
        return 2;
    }
    if (!hdf5.initializeDatasets())
    {
        hdf5.closeFile();
        return 3;
    }
    if (!hdf5.flush())
    {
        hdf5.closeFile();
        return 4;
    }
    hdf5.closeFile();

    if (!std::filesystem::exists(path))
    {
        return 5;
    }

    if (!hdf5.loadFile(path))
    {
        std::filesystem::remove(path);
        return 6;
    }
    hdf5.closeFile();

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + ".recovery.h5", ec);
    return 0;
}

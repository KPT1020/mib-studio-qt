// RAII temporary directory for tests; removed on destruction.
#pragma once

#include <filesystem>
#include <random>
#include <stdexcept>
#include <string>

namespace mib::test {

class TempDir {
public:
    explicit TempDir(const std::string& prefix = "mib_test")
    {
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<unsigned long long> dist;
        for (int attempt = 0; attempt < 100; ++attempt) {
            auto p = std::filesystem::temp_directory_path() /
                     (prefix + "_" + std::to_string(dist(gen)));
            std::error_code ec;
            if (std::filesystem::create_directories(p, ec)) {
                path_ = p;
                return;
            }
        }
        throw std::runtime_error("TempDir: failed to create temporary directory");
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const { return path_; }
    std::filesystem::path operator/(const std::string& leaf) const { return path_ / leaf; }

private:
    std::filesystem::path path_;
};

} // namespace mib::test

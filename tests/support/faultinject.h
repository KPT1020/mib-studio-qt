// Fault-injection helpers for storage/IO robustness tests.
#pragma once

#include <filesystem>
#include <string>
#include <system_error>

namespace mib::test {

// A path whose total length exceeds the Windows MAX_PATH (260) limit.
inline std::filesystem::path longPath(const std::filesystem::path& base,
                                      const std::string& leaf = "experiment.h5")
{
    const std::string seg(80, 'x');
    return base / seg / seg / seg / leaf;
}

// Make a directory read-only (best effort, cross-platform via std::filesystem
// permissions). Returns false if the platform/filesystem ignored it.
inline bool makeReadOnly(const std::filesystem::path& dir)
{
    std::error_code ec;
    std::filesystem::permissions(
        dir,
        std::filesystem::perms::owner_write | std::filesystem::perms::group_write |
            std::filesystem::perms::others_write,
        std::filesystem::perm_options::remove, ec);
    return !ec;
}

inline void restoreWritable(const std::filesystem::path& dir)
{
    std::error_code ec;
    std::filesystem::permissions(dir, std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::add, ec);
}

} // namespace mib::test

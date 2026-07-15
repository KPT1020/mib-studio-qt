#pragma once

#include <filesystem>
#include <string>

namespace backend::processing {

struct ProcessingCoreCacheRequest {
    std::filesystem::path sourcePath;
    std::filesystem::path cacheRoot;
    std::string version;
    std::string sha256;
    std::string filename;
};

struct ProcessingCoreCacheResult {
    std::filesystem::path pluginPath;
    std::filesystem::path readyMetadataPath;
    bool reused{false};
    std::string error;

    explicit operator bool() const noexcept { return !pluginPath.empty() && error.empty(); }
};

// Materializes an already-downloaded artifact into the content-addressed
// persistent cache. Download and platform-signature validation remain separate;
// loadProcessingCorePlugin performs digest and trust verification again at
// the final load boundary.
ProcessingCoreCacheResult prepareProcessingCoreArtifact(
    const ProcessingCoreCacheRequest& request);

} // namespace backend::processing

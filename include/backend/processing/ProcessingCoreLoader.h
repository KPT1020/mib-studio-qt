#pragma once

#include "backend/processing/IProcessingKernel.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace backend::processing {

struct ProcessingCoreLoadRequirements {
    std::string expectedVersion;
    uint32_t expectedContractVersion{1};
    uint32_t expectedEngineAbiVersion{1};
    std::string expectedRuntimeFingerprint;
    std::string artifactSha256;
    std::string releaseTag;
    std::string manifestSha256;
    // The desktop supplies an Authenticode verifier in production. Tests and
    // non-Windows backend builds can inject a deterministic verifier.
    std::function<bool(const std::filesystem::path&, std::string&)> trustVerifier;
};

struct ProcessingCoreLoadResult {
    std::shared_ptr<IProcessingKernel> kernel;
    std::string error;

    explicit operator bool() const noexcept { return kernel != nullptr; }
};

ProcessingCoreLoadResult loadProcessingCorePlugin(
    const std::filesystem::path& absolutePluginPath,
    const ProcessingCoreLoadRequirements& requirements);

// Qt-free streaming SHA-256 used before a native module is loaded.
std::string processingCoreFileSha256(const std::filesystem::path& path,
                                     std::string* error = nullptr);

// Windows production verifier: validates the embedded Authenticode chain and
// requires the leaf certificate's DER SubjectPublicKeyInfo SHA-256 to match
// the compiled allowlist value. Non-Windows builds fail closed.
bool verifyProcessingCoreAuthenticode(const std::filesystem::path& path,
                                      const std::string& approvedSubjectPublicKeyInfoSha256,
                                      std::string& error);

} // namespace backend::processing

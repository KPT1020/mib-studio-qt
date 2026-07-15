#include "backend/processing/IProcessingKernel.h"
#include "backend/processing/ProcessingCoreLoader.h"
#include "support/assert.h"

#include <filesystem>
#include <string>

#include <opencv2/core.hpp>

namespace {

backend::processing::ProcessingCoreLoadRequirements requirementsFor(
    const std::filesystem::path& path) {
    const auto identity = backend::processing::bundledProcessingCoreIdentity();
    backend::processing::ProcessingCoreLoadRequirements requirements;
    requirements.expectedVersion = identity.version;
    requirements.expectedContractVersion = identity.contractVersion;
    requirements.expectedEngineAbiVersion = identity.engineAbiVersion;
    requirements.expectedRuntimeFingerprint = identity.runtimeFingerprint;
    requirements.artifactSha256 = backend::processing::processingCoreFileSha256(path);
    requirements.manifestSha256 = std::string(64, 'd');
    requirements.releaseTag = "fixture-" + path.filename().string();
    requirements.trustVerifier = [](const std::filesystem::path&, std::string&) { return true; };
    return requirements;
}

backend::processing::ProcessingCoreLoadResult load(const std::filesystem::path& path) {
    return backend::processing::loadProcessingCorePlugin(
        std::filesystem::absolute(path), requirementsFor(std::filesystem::absolute(path)));
}

} // namespace

int main(int argc, char** argv) {
    MIB_REQUIRE(argc == 6,
                "good, truncated, incompatible, malformed, and throwing fixture paths provided");

    const auto good = load(argv[1]);
    MIB_REQUIRE(good, good.error);
    cv::Mat input = cv::Mat::zeros(7, 9, CV_8UC1);
    input.at<uint8_t>(3, 4) = 255u;
    cv::Mat mask;
    std::string error;
    MIB_REQUIRE(good.kernel->processMask(input, {}, {}, {0, 0, 9, 7}, mask, &error), error);
    MIB_EXPECT(cv::countNonZero(mask != input) == 0,
               "independent compatible C fixture executes across the public ABI");
    bool empty = true;
    MIB_REQUIRE(good.kernel->isEmpty(input, {}, {}, {0, 0, 9, 7}, empty, &error), error);
    MIB_EXPECT(!empty, "independent compatible C fixture returns an empty-frame decision");
    MIB_REQUIRE(good.kernel->reset(&error), error);

    const auto truncated = load(argv[2]);
    MIB_EXPECT(!truncated && truncated.error.find("incomplete API table") != std::string::npos,
               "truncated real API table is rejected");

    const auto incompatible = load(argv[3]);
    MIB_EXPECT(!incompatible && incompatible.error.find("descriptor") != std::string::npos,
               "incompatible real descriptor is rejected");

    const auto malformed = load(argv[4]);
    MIB_EXPECT(!malformed && malformed.error.find("incomplete API table") != std::string::npos,
               "malformed real function table is rejected");

    const auto throwing = load(argv[5]);
    MIB_REQUIRE(throwing, throwing.error);
    error.clear();
    MIB_EXPECT(!throwing.kernel->processMask(input, {}, {}, {0, 0, 9, 7}, mask, &error),
               "plugin catches its internal exception and reports a C status");
    MIB_EXPECT(error.find("injected fixture exception") != std::string::npos,
               "caught plugin exception diagnostic crosses only the caller-owned error buffer");

    return mib::test::exitCode();
}

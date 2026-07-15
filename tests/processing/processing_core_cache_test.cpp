#include "backend/processing/ProcessingCoreCache.h"
#include "backend/processing/ProcessingCoreLoader.h"
#include "support/assert.h"
#include "support/tempdir.h"
#include "support/watchdog.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <thread>

int main(int argc, char** argv) {
    MIB_REQUIRE(argc == 2, "plugin path argument provided");
    mib::test::Watchdog watchdog(20);
    mib::test::TempDir temporary("processing-core-cache");
    const auto source = std::filesystem::absolute(argv[1]);
    std::string hashError;
    const auto sha = backend::processing::processingCoreFileSha256(source, &hashError);
    MIB_REQUIRE(sha.size() == 64, hashError);

    backend::processing::ProcessingCoreCacheRequest request;
    request.sourcePath = source;
    request.cacheRoot = std::filesystem::absolute(temporary.path() / "cache");
    request.version = "0.1.0";
    request.sha256 = sha;
    request.filename = source.filename().string();

    watchdog.mark("first prepare");
    const auto first = backend::processing::prepareProcessingCoreArtifact(request);
    MIB_REQUIRE(first, first.error);
    MIB_EXPECT(!first.reused, "first preparation commits a new entry");
    MIB_EXPECT(std::filesystem::is_regular_file(first.pluginPath), "cached plugin exists");
    MIB_EXPECT(std::filesystem::is_regular_file(first.readyMetadataPath), "ready marker exists");

    watchdog.mark("cache reuse");
    const auto second = backend::processing::prepareProcessingCoreArtifact(request);
    MIB_REQUIRE(second, second.error);
    MIB_EXPECT(second.reused && second.pluginPath == first.pluginPath,
               "verified cache entry is reused");

    watchdog.mark("concurrent prepare");
    std::filesystem::remove_all(request.cacheRoot);
    backend::processing::ProcessingCoreCacheResult concurrentA;
    backend::processing::ProcessingCoreCacheResult concurrentB;
    std::thread a([&] { concurrentA = backend::processing::prepareProcessingCoreArtifact(request); });
    std::thread b([&] { concurrentB = backend::processing::prepareProcessingCoreArtifact(request); });
    a.join();
    b.join();
    MIB_REQUIRE(concurrentA, concurrentA.error);
    MIB_REQUIRE(concurrentB, concurrentB.error);
    MIB_EXPECT(concurrentA.pluginPath == concurrentB.pluginPath,
               "concurrent preparation converges on one content-addressed path");
    MIB_EXPECT(concurrentA.reused != concurrentB.reused,
               "one preparer commits and the other reuses");

    watchdog.mark("stale lock recovery");
    std::filesystem::remove_all(request.cacheRoot);
    const auto staleLock = request.cacheRoot / request.version / request.sha256 / ".prepare.lock";
    std::filesystem::create_directories(staleLock);
    std::filesystem::last_write_time(
        staleLock, std::filesystem::file_time_type::clock::now() - std::chrono::hours(1));
    const auto recovered = backend::processing::prepareProcessingCoreArtifact(request);
    MIB_REQUIRE(recovered, recovered.error);
    MIB_EXPECT(std::filesystem::is_regular_file(recovered.pluginPath),
               "stale crashed-preparer lock is recovered conservatively");

    watchdog.mark("publisher-compatible names");
    auto canonical = request;
    canonical.version = "0.1.0+gpu!rc1";
    canonical.filename = "mib_processing_core-0.1.0+gpu!rc1.so";
    const auto canonicalResult =
        backend::processing::prepareProcessingCoreArtifact(canonical);
    MIB_REQUIRE(canonicalResult, canonicalResult.error);
    MIB_EXPECT(canonicalResult.pluginPath.filename() == canonical.filename,
               "publisher-supported plus and exclamation characters are accepted");

    watchdog.mark("failure guards");
    auto unsafe = request;
    unsafe.version = "../escape";
    MIB_EXPECT(!backend::processing::prepareProcessingCoreArtifact(unsafe),
               "unsafe version cannot escape cache root");
    unsafe.version = "+0.1.0";
    MIB_EXPECT(!backend::processing::prepareProcessingCoreArtifact(unsafe),
               "canonical version must begin with a letter or digit");

    const std::array<const char*, 7> reservedFilenames{
        ".ready.json",
        ".READY.JSON",
        ".prepare.lock",
        ".PREPARE.LOCK",
        ".ready.json.tmp.42",
        ".prepare.lock.tmp.7",
        "mib_processing_core.dll.tmp.0",
    };
    for (const char* filename : reservedFilenames) {
        auto reserved = request;
        reserved.filename = filename;
        MIB_EXPECT(!backend::processing::prepareProcessingCoreArtifact(reserved),
                   std::string("reserved cache filename is rejected: ") + filename);
    }

    auto nestedFilename = request;
    nestedFilename.filename = "nested/core.dll";
    MIB_EXPECT(!backend::processing::prepareProcessingCoreArtifact(nestedFilename),
               "artifact filename cannot contain path separators");
    nestedFilename.filename = "..\\core.dll";
    MIB_EXPECT(!backend::processing::prepareProcessingCoreArtifact(nestedFilename),
               "artifact filename cannot contain Windows path separators");

    auto wrongHash = request;
    wrongHash.sha256 = std::string(64, '0');
    MIB_EXPECT(!backend::processing::prepareProcessingCoreArtifact(wrongHash),
               "source digest mismatch fails closed");
    return mib::test::exitCode();
}

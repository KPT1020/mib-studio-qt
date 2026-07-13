#include "backend/processing/ProcessingCoreCache.h"

#include "backend/processing/ProcessingCoreLoader.h"

#include <atomic>
#include <chrono>
#include <cctype>
#include <fstream>
#include <thread>

namespace backend::processing {
namespace {

bool canonicalVersion(const std::string& value) {
    if (value.empty() || value == "." || value == "..") return false;
    if (!std::isalnum(static_cast<unsigned char>(value.front()))) return false;
    for (unsigned char character : value) {
        if (!(std::isalnum(character) || character == '.' || character == '-' ||
              character == '_' || character == '+' || character == '!')) {
            return false;
        }
    }
    return true;
}

bool hasTemporarySuffix(const std::string& value) {
    const auto marker = value.rfind(".tmp.");
    if (marker == std::string::npos || marker + 5 == value.size()) return false;
    for (auto index = marker + 5; index < value.size(); ++index) {
        if (!std::isdigit(static_cast<unsigned char>(value[index]))) return false;
    }
    return true;
}

bool safeArtifactFilename(const std::string& value) {
    if (value.empty() || value == "." || value == "..") return false;
    for (unsigned char character : value) {
        if (!(std::isalnum(character) || character == '.' || character == '-' ||
              character == '_' || character == '+' || character == '!')) {
            return false;
        }
    }

    std::string lowercase = value;
    for (char& character : lowercase) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return lowercase != ".ready.json" && lowercase != ".prepare.lock" &&
           !hasTemporarySuffix(lowercase);
}

class DirectoryLock {
public:
    bool acquire(const std::filesystem::path& path, std::string& error) {
        path_ = path;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        std::error_code ec;
        while (std::chrono::steady_clock::now() < deadline) {
            ec.clear();
            if (std::filesystem::create_directory(path_, ec)) {
                std::ofstream owner(path_ / "owner", std::ios::trunc);
                owner << "created_by=mib-studio\n";
                acquired_ = true;
                return true;
            }
            if (ec && ec != std::errc::file_exists) {
                error = "cannot create processing-core cache lock: " + ec.message();
                return false;
            }
            ec.clear();
            const auto modified = std::filesystem::last_write_time(path_, ec);
            if (!ec && std::filesystem::file_time_type::clock::now() - modified >
                           std::chrono::minutes(10)) {
                std::filesystem::remove_all(path_, ec);
                if (!ec) continue;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        error = "timed out waiting for processing-core cache lock";
        return false;
    }

    ~DirectoryLock() {
        if (!acquired_) return;
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

private:
    std::filesystem::path path_;
    bool acquired_{false};
};

std::filesystem::path temporarySibling(const std::filesystem::path& destination) {
    static std::atomic<uint64_t> sequence{0};
    return destination.string() + ".tmp." +
           std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
}

bool cacheIsReady(const std::filesystem::path& artifact,
                  const std::filesystem::path& ready,
                  const std::string& expectedSha) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(artifact, ec) || ec ||
        !std::filesystem::is_regular_file(ready, ec) || ec) {
        return false;
    }
    return processingCoreFileSha256(artifact) == expectedSha;
}

} // namespace

ProcessingCoreCacheResult prepareProcessingCoreArtifact(
    const ProcessingCoreCacheRequest& request) {
    ProcessingCoreCacheResult result;
    if (!canonicalVersion(request.version)) {
        result.error = "processing-core version must be a canonical path-safe version";
        return result;
    }
    if (!safeArtifactFilename(request.filename)) {
        result.error = "processing-core filename must be a safe non-reserved cache filename";
        return result;
    }
    if (request.sha256.size() != 64 ||
        request.sha256.find_first_not_of("0123456789abcdef") != std::string::npos) {
        result.error = "processing-core SHA-256 must be 64 lowercase hexadecimal characters";
        return result;
    }
    if (request.cacheRoot.empty() || !request.cacheRoot.is_absolute()) {
        result.error = "processing-core cache root must be absolute";
        return result;
    }

    std::string hashError;
    const auto sourceSha = processingCoreFileSha256(request.sourcePath, &hashError);
    if (sourceSha != request.sha256) {
        result.error = sourceSha.empty() ? hashError : "downloaded processing-core SHA-256 mismatch";
        return result;
    }

    const auto versionDirectory = request.cacheRoot / request.version / request.sha256;
    const auto destination = versionDirectory / request.filename;
    const auto ready = versionDirectory / ".ready.json";
    std::error_code ec;
    std::filesystem::create_directories(versionDirectory, ec);
    if (ec) {
        result.error = "cannot create processing-core cache directory: " + ec.message();
        return result;
    }

    DirectoryLock lock;
    if (!lock.acquire(versionDirectory / ".prepare.lock", result.error)) return result;
    if (cacheIsReady(destination, ready, request.sha256)) {
        result.pluginPath = destination;
        result.readyMetadataPath = ready;
        result.reused = true;
        return result;
    }

    std::filesystem::remove(destination, ec);
    std::filesystem::remove(ready, ec);
    const auto stagedArtifact = temporarySibling(destination);
    const auto stagedReady = temporarySibling(ready);
    std::filesystem::copy_file(request.sourcePath, stagedArtifact,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec || processingCoreFileSha256(stagedArtifact) != request.sha256) {
        std::filesystem::remove(stagedArtifact, ec);
        result.error = ec ? "cannot stage processing core: " + ec.message()
                          : "staged processing-core SHA-256 mismatch";
        return result;
    }

    {
        std::ofstream metadata(stagedReady, std::ios::binary | std::ios::trunc);
        metadata << "{\n  \"version\": \"" << request.version << "\",\n"
                 << "  \"sha256\": \"" << request.sha256 << "\",\n"
                 << "  \"filename\": \"" << request.filename << "\"\n}\n";
        if (!metadata) {
            std::filesystem::remove(stagedArtifact, ec);
            std::filesystem::remove(stagedReady, ec);
            result.error = "cannot write processing-core ready metadata";
            return result;
        }
    }

    std::filesystem::rename(stagedArtifact, destination, ec);
    if (!ec) std::filesystem::rename(stagedReady, ready, ec);
    if (ec) {
        std::filesystem::remove(stagedArtifact, ec);
        std::filesystem::remove(stagedReady, ec);
        result.error = "cannot atomically commit processing-core cache entry: " + ec.message();
        return result;
    }
    result.pluginPath = destination;
    result.readyMetadataPath = ready;
    return result;
}

} // namespace backend::processing

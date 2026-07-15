// Linux sibling of processing_core_authenticode_test: proves the Ed25519
// detached-signature verifier accepts exactly one approved signer/artifact
// pair and fails closed for every tampered or substituted input, then loads
// the signed artifact through the content-addressed cache and dlopen.
#include "backend/processing/IProcessingKernel.h"
#include "backend/processing/ProcessingCoreCache.h"
#include "backend/processing/ProcessingCoreLoader.h"
#include "support/assert.h"

#include <openssl/evp.h>
#include <openssl/x509.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

using EvpKey = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;

EvpKey generateKey(int keyType) {
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> context(
        EVP_PKEY_CTX_new_id(keyType, nullptr), &EVP_PKEY_CTX_free);
    MIB_REQUIRE(context && EVP_PKEY_keygen_init(context.get()) == 1,
                "signer key generation initializes");
    if (keyType == EVP_PKEY_EC) {
        MIB_REQUIRE(EVP_PKEY_CTX_set_ec_paramgen_curve_nid(context.get(), NID_X9_62_prime256v1) ==
                        1,
                    "P-256 curve selected for the non-Ed25519 control key");
    }
    EVP_PKEY* raw = nullptr;
    MIB_REQUIRE(EVP_PKEY_keygen(context.get(), &raw) == 1, "signer key generates");
    return EvpKey(raw, &EVP_PKEY_free);
}

std::vector<uint8_t> subjectPublicKeyInfoDer(EVP_PKEY* key) {
    const int size = i2d_PUBKEY(key, nullptr);
    MIB_REQUIRE(size > 0, "SPKI encodes");
    std::vector<uint8_t> der(static_cast<size_t>(size));
    uint8_t* cursor = der.data();
    MIB_REQUIRE(i2d_PUBKEY(key, &cursor) == size, "SPKI encoding is stable");
    return der;
}

std::string base64(const std::vector<uint8_t>& bytes) {
    std::string encoded(4 * ((bytes.size() + 2) / 3), '\0');
    const int written = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(encoded.data()),
                                        bytes.data(), static_cast<int>(bytes.size()));
    MIB_REQUIRE(written >= 0 && static_cast<size_t>(written) == encoded.size(),
                "base64 encoding is canonical");
    return encoded;
}

std::vector<uint8_t> readFileBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    MIB_REQUIRE(stream, "artifact opens for signing");
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(stream)),
                                std::istreambuf_iterator<char>());
}

std::vector<uint8_t> signEd25519(EVP_PKEY* key, const std::vector<uint8_t>& payload) {
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(),
                                                                    &EVP_MD_CTX_free);
    MIB_REQUIRE(context &&
                    EVP_DigestSignInit(context.get(), nullptr, nullptr, nullptr, key) == 1,
                "Ed25519 signing initializes");
    size_t signatureSize = 0;
    MIB_REQUIRE(EVP_DigestSign(context.get(), nullptr, &signatureSize, payload.data(),
                               payload.size()) == 1 &&
                    signatureSize == 64,
                "Ed25519 signature is 64 bytes");
    std::vector<uint8_t> signature(signatureSize);
    MIB_REQUIRE(EVP_DigestSign(context.get(), signature.data(), &signatureSize, payload.data(),
                               payload.size()) == 1,
                "Ed25519 signing succeeds");
    return signature;
}

} // namespace

int main(int argc, char** argv) {
    MIB_REQUIRE(argc == 2, "plugin path argument provided");
    const std::filesystem::path pluginPath = std::filesystem::absolute(argv[1]);

    const auto signerKey = generateKey(EVP_PKEY_ED25519);
    const auto signerSpki = subjectPublicKeyInfoDer(signerKey.get());
    MIB_REQUIRE(signerSpki.size() == 44, "Ed25519 SPKI is 44 DER bytes");
    const std::string approvedPin =
        backend::processing::processingCoreBytesSha256(signerSpki.data(), signerSpki.size());
    const auto artifactBytes = readFileBytes(pluginPath);
    const auto artifactSignature = signEd25519(signerKey.get(), artifactBytes);

    backend::processing::ProcessingCoreDetachedSignature signature;
    signature.publicKeySpkiDerBase64 = base64(signerSpki);
    signature.signatureBase64 = base64(artifactSignature);

    std::string error;
    MIB_REQUIRE(backend::processing::verifyProcessingCoreEd25519(pluginPath, signature,
                                                                 approvedPin, error),
                error);

    error.clear();
    std::string wrongPin(64, '0');
    if (wrongPin == approvedPin) wrongPin.assign(64, '1');
    MIB_EXPECT(!backend::processing::verifyProcessingCoreEd25519(pluginPath, signature,
                                                                 wrongPin, error),
               "valid signature from an unapproved SPKI is rejected");

    error.clear();
    MIB_EXPECT(!backend::processing::verifyProcessingCoreEd25519(pluginPath, signature, "",
                                                                 error),
               "an empty compiled pin fails closed");

    const auto substituteKey = generateKey(EVP_PKEY_ED25519);
    backend::processing::ProcessingCoreDetachedSignature substituteSignature = signature;
    substituteSignature.signatureBase64 =
        base64(signEd25519(substituteKey.get(), artifactBytes));
    error.clear();
    MIB_EXPECT(!backend::processing::verifyProcessingCoreEd25519(pluginPath, substituteSignature,
                                                                 approvedPin, error),
               "a signature from a different key over the same bytes is rejected");

    const auto ecKey = generateKey(EVP_PKEY_EC);
    const auto ecSpki = subjectPublicKeyInfoDer(ecKey.get());
    backend::processing::ProcessingCoreDetachedSignature nonEd25519 = signature;
    nonEd25519.publicKeySpkiDerBase64 = base64(ecSpki);
    const std::string ecPin =
        backend::processing::processingCoreBytesSha256(ecSpki.data(), ecSpki.size());
    error.clear();
    MIB_EXPECT(!backend::processing::verifyProcessingCoreEd25519(pluginPath, nonEd25519, ecPin,
                                                                 error),
               "a non-Ed25519 public key is rejected even when its pin matches");

    auto corruptedSignatureBytes = artifactSignature;
    corruptedSignatureBytes[10] ^= 0x5a;
    backend::processing::ProcessingCoreDetachedSignature corrupted = signature;
    corrupted.signatureBase64 = base64(corruptedSignatureBytes);
    error.clear();
    MIB_EXPECT(!backend::processing::verifyProcessingCoreEd25519(pluginPath, corrupted,
                                                                 approvedPin, error),
               "a corrupted signature is rejected");

    backend::processing::ProcessingCoreDetachedSignature missing = signature;
    missing.signatureBase64.clear();
    error.clear();
    MIB_EXPECT(!backend::processing::verifyProcessingCoreEd25519(pluginPath, missing,
                                                                 approvedPin, error),
               "a missing signature fails closed");

    backend::processing::ProcessingCoreDetachedSignature malformed = signature;
    malformed.publicKeySpkiDerBase64 = "!!!not-base64!!!";
    error.clear();
    MIB_EXPECT(!backend::processing::verifyProcessingCoreEd25519(pluginPath, malformed,
                                                                 approvedPin, error),
               "a malformed public key encoding fails closed");

    const auto scratch =
        std::filesystem::temp_directory_path() /
        ("mib-ed25519-test-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(scratch);

    const auto tamperedPath = scratch / pluginPath.filename();
    std::filesystem::copy_file(pluginPath, tamperedPath,
                               std::filesystem::copy_options::overwrite_existing);
    {
        std::fstream stream(tamperedPath, std::ios::binary | std::ios::in | std::ios::out);
        MIB_REQUIRE(stream, "tamper fixture opens for mutation");
        stream.seekg(0, std::ios::end);
        const auto size = static_cast<std::streamoff>(stream.tellg());
        MIB_REQUIRE(size > 1024, "artifact is large enough for deterministic mutation");
        const std::streamoff offset = size / 4;
        stream.seekg(offset);
        char value = 0;
        stream.read(&value, 1);
        MIB_REQUIRE(stream.gcount() == 1, "tamper fixture byte can be read");
        value ^= static_cast<char>(0x5a);
        stream.seekp(offset);
        stream.write(&value, 1);
        stream.flush();
        MIB_REQUIRE(stream.good(), "tamper fixture byte is persisted");
    }
    error.clear();
    MIB_EXPECT(!backend::processing::verifyProcessingCoreEd25519(tamperedPath, signature,
                                                                 approvedPin, error),
               "post-sign artifact mutation is rejected");

    // Full production path: stage the signed artifact into the
    // content-addressed cache, then dlopen it behind the Ed25519 verifier.
    const auto bundledIdentity = backend::processing::bundledProcessingCoreIdentity();
    std::string hashError;
    const std::string artifactSha256 =
        backend::processing::processingCoreFileSha256(pluginPath, &hashError);
    MIB_REQUIRE(artifactSha256.size() == 64, hashError);

    backend::processing::ProcessingCoreCacheRequest cacheRequest;
    cacheRequest.sourcePath = pluginPath;
    cacheRequest.cacheRoot = scratch / "cache";
    cacheRequest.version = bundledIdentity.version;
    cacheRequest.sha256 = artifactSha256;
    cacheRequest.filename = pluginPath.filename().string();
    const auto cached = backend::processing::prepareProcessingCoreArtifact(cacheRequest);
    MIB_REQUIRE(cached, cached.error);

    int trustCalls = 0;
    backend::processing::ProcessingCoreLoadRequirements requirements;
    requirements.expectedVersion = bundledIdentity.version;
    requirements.expectedContractVersion = bundledIdentity.contractVersion;
    requirements.expectedEngineAbiVersion = bundledIdentity.engineAbiVersion;
    requirements.expectedRuntimeFingerprint = bundledIdentity.runtimeFingerprint;
    requirements.artifactSha256 = artifactSha256;
    requirements.releaseTag = "mib-processing-v" + bundledIdentity.version;
    requirements.manifestSha256 = std::string(64, 'c');
    requirements.trustVerifier = [&](const std::filesystem::path& candidate,
                                     std::string& trustError) {
        ++trustCalls;
        return backend::processing::verifyProcessingCoreEd25519(candidate, signature,
                                                                approvedPin, trustError);
    };
    const auto loaded =
        backend::processing::loadProcessingCorePlugin(cached.pluginPath, requirements);
    MIB_REQUIRE(loaded, loaded.error);
    MIB_EXPECT(trustCalls == 1, "Ed25519 verifier ran exactly once for the cache load");
    MIB_EXPECT(loaded.kernel->identity().source == "plugin",
               "signed cache-staged plugin activates as a dynamic kernel");

    std::error_code cleanupError;
    std::filesystem::remove_all(scratch, cleanupError);
    return mib::test::exitCode();
}

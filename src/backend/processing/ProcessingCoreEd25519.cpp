#include "backend/processing/ProcessingCoreLoader.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#if !defined(MIB_PROCESSING_CORE_HAVE_OPENSSL)
#define MIB_PROCESSING_CORE_HAVE_OPENSSL 0
#endif

#if defined(__linux__) && MIB_PROCESSING_CORE_HAVE_OPENSSL
#include <openssl/evp.h>
#include <openssl/x509.h>

#include <memory>
#endif

namespace backend::processing {
#if defined(__linux__) && MIB_PROCESSING_CORE_HAVE_OPENSSL
namespace {

bool decodeBase64Strict(const std::string& text, std::vector<uint8_t>& out) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    out.clear();
    if (text.empty() || text.size() % 4 != 0) return false;
    size_t padding = 0;
    if (text.size() >= 1 && text[text.size() - 1] == '=') ++padding;
    if (text.size() >= 2 && text[text.size() - 2] == '=') ++padding;
    uint32_t accumulator = 0;
    int accumulatedBits = 0;
    for (size_t i = 0; i < text.size() - padding; ++i) {
        const char* position = std::char_traits<char>::find(kAlphabet, 64, text[i]);
        if (!position) return false;
        accumulator = (accumulator << 6u) | static_cast<uint32_t>(position - kAlphabet);
        accumulatedBits += 6;
        if (accumulatedBits >= 8) {
            accumulatedBits -= 8;
            out.push_back(static_cast<uint8_t>((accumulator >> accumulatedBits) & 0xffu));
        }
    }
    // Reject non-canonical encodings that smuggle set bits into the padding.
    if (accumulatedBits > 0 && (accumulator & ((1u << accumulatedBits) - 1u)) != 0) return false;
    return true;
}

bool sha256HexShape(const std::string& value) {
    return value.size() == 64 &&
           value.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
}

} // namespace
#endif

bool verifyProcessingCoreEd25519(const std::filesystem::path& path,
                                 const ProcessingCoreDetachedSignature& signature,
                                 const std::string& approvedSubjectPublicKeyInfoSha256,
                                 std::string& error) {
#if !defined(__linux__) || !MIB_PROCESSING_CORE_HAVE_OPENSSL
    (void)path;
    (void)signature;
    (void)approvedSubjectPublicKeyInfoSha256;
    error = "Ed25519 detached-signature verification is only available on Linux "
            "builds with OpenSSL";
    return false;
#else
    if (!sha256HexShape(approvedSubjectPublicKeyInfoSha256)) {
        error = "approved signer SPKI SHA-256 is not compiled into the application";
        return false;
    }
    std::vector<uint8_t> subjectPublicKeyInfo;
    if (!decodeBase64Strict(signature.publicKeySpkiDerBase64, subjectPublicKeyInfo) ||
        subjectPublicKeyInfo.size() != 44) {
        error = "processing core signer public key is not a canonical Ed25519 SPKI";
        return false;
    }
    std::vector<uint8_t> signatureBytes;
    if (!decodeBase64Strict(signature.signatureBase64, signatureBytes) ||
        signatureBytes.size() != 64) {
        error = "processing core detached signature is not a canonical Ed25519 signature";
        return false;
    }

    const std::string actualSpkiSha256 =
        processingCoreBytesSha256(subjectPublicKeyInfo.data(), subjectPublicKeyInfo.size());
    std::string expected = approvedSubjectPublicKeyInfoSha256;
    std::transform(expected.begin(), expected.end(), expected.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (actualSpkiSha256 != expected) {
        error = "processing core signer public key is not approved";
        return false;
    }

    const uint8_t* derCursor = subjectPublicKeyInfo.data();
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> publicKey(
        d2i_PUBKEY(nullptr, &derCursor, static_cast<long>(subjectPublicKeyInfo.size())),
        &EVP_PKEY_free);
    if (!publicKey || EVP_PKEY_base_id(publicKey.get()) != EVP_PKEY_ED25519) {
        error = "processing core signer public key is not an Ed25519 key";
        return false;
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "cannot read processing core artifact for signature verification";
        return false;
    }
    std::vector<uint8_t> artifact((std::istreambuf_iterator<char>(stream)),
                                  std::istreambuf_iterator<char>());
    if (!stream.good() && !stream.eof()) {
        error = "cannot read processing core artifact for signature verification";
        return false;
    }
    if (artifact.empty()) {
        error = "processing core artifact is empty";
        return false;
    }

    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(),
                                                                    &EVP_MD_CTX_free);
    if (!context ||
        EVP_DigestVerifyInit(context.get(), nullptr, nullptr, nullptr, publicKey.get()) != 1) {
        error = "cannot initialize Ed25519 signature verification";
        return false;
    }
    if (EVP_DigestVerify(context.get(), signatureBytes.data(), signatureBytes.size(),
                         artifact.data(), artifact.size()) != 1) {
        error = "Ed25519 signature does not match the processing core artifact";
        return false;
    }
    return true;
#endif
}

} // namespace backend::processing

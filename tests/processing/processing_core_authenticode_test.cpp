#include "backend/processing/ProcessingCoreLoader.h"
#include "support/assert.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
    MIB_REQUIRE(argc == 4, "unsigned path, signed path, and signer SPKI are provided");
    const std::filesystem::path unsignedPath = std::filesystem::absolute(argv[1]);
    const std::filesystem::path signedPath = std::filesystem::absolute(argv[2]);
    std::string signerSpki = argv[3];
    std::transform(signerSpki.begin(), signerSpki.end(), signerSpki.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    MIB_REQUIRE(signerSpki.size() == 64, "fixture signer SPKI has canonical length");

    std::string error;
    MIB_EXPECT(!backend::processing::verifyProcessingCoreAuthenticode(
                   unsignedPath, signerSpki, error),
               "unsigned fixture is rejected by WinVerifyTrust");

    error.clear();
    MIB_REQUIRE(backend::processing::verifyProcessingCoreAuthenticode(
                    signedPath, signerSpki, error),
                error);

    std::string wrongSigner(64, '0');
    if (wrongSigner == signerSpki) wrongSigner.assign(64, '1');
    error.clear();
    MIB_EXPECT(!backend::processing::verifyProcessingCoreAuthenticode(
                   signedPath, wrongSigner, error),
               "valid signature from an unapproved SPKI is rejected");

    const auto tamperedPath = signedPath.parent_path() /
                              (signedPath.stem().string() + "-tampered" +
                               signedPath.extension().string());
    std::filesystem::copy_file(signedPath, tamperedPath,
                               std::filesystem::copy_options::overwrite_existing);
    {
        std::fstream stream(tamperedPath, std::ios::binary | std::ios::in | std::ios::out);
        MIB_REQUIRE(stream, "tamper fixture opens for mutation");
        stream.seekg(0, std::ios::end);
        const auto size = static_cast<std::streamoff>(stream.tellg());
        MIB_REQUIRE(size > 1024, "signed fixture is large enough for deterministic mutation");
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
    MIB_EXPECT(!backend::processing::verifyProcessingCoreAuthenticode(
                   tamperedPath, signerSpki, error),
               "post-sign mutation is rejected by WinVerifyTrust");
    std::error_code cleanupError;
    std::filesystem::remove(tamperedPath, cleanupError);

    return mib::test::exitCode();
}

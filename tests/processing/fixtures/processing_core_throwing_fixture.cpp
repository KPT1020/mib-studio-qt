#include "backend/processing/ProcessingCoreAbi.h"

#include <cstring>
#include <new>
#include <stdexcept>

#ifndef MIB_PROCESSING_CORE_VERSION
#  define MIB_PROCESSING_CORE_VERSION "0.1.0"
#endif

#define MIB_FIXTURE_STRINGIFY_INNER(value) #value
#define MIB_FIXTURE_STRINGIFY(value) MIB_FIXTURE_STRINGIFY_INNER(value)

#if defined(_MSC_VER)
#  define MIB_FIXTURE_RUNTIME_FINGERPRINT \
    "windows-x86_64-msvc" MIB_FIXTURE_STRINGIFY(_MSC_VER) "-md-cxx17"
#elif defined(__clang__)
#  define MIB_FIXTURE_RUNTIME_FINGERPRINT \
    "portable-clang" MIB_FIXTURE_STRINGIFY(__clang_major__) "-cxx17"
#elif defined(__GNUC__)
#  define MIB_FIXTURE_RUNTIME_FINGERPRINT \
    "portable-gcc" MIB_FIXTURE_STRINGIFY(__GNUC__) "-cxx17"
#else
#  define MIB_FIXTURE_RUNTIME_FINGERPRINT "portable-unknown-cxx17"
#endif

namespace {

void writeError(char* error, size_t capacity, const char* message) {
    if (!error || capacity == 0) return;
    std::strncpy(error, message, capacity - 1);
    error[capacity - 1] = '\0';
}

const mib_processing_core_descriptor* MIB_PROCESSING_CALL descriptor() {
    static const mib_processing_core_descriptor value{
        sizeof(mib_processing_core_descriptor), MIB_PROCESSING_ENGINE_ABI_VERSION,
        MIB_PROCESSING_CONTRACT_VERSION, 0u, MIB_PROCESSING_CORE_VERSION,
        "independent-throwing-fixture", MIB_FIXTURE_RUNTIME_FINGERPRINT, {}};
    return &value;
}

mib_processing_status MIB_PROCESSING_CALL createContext(
    mib_processing_context* output, char*, size_t) {
    if (!output) return MIB_PROCESSING_STATUS_INVALID_ARGUMENT;
    *output = new (std::nothrow) unsigned char(0);
    return *output ? MIB_PROCESSING_STATUS_OK : MIB_PROCESSING_STATUS_INTERNAL_ERROR;
}

void MIB_PROCESSING_CALL destroyContext(mib_processing_context context) {
    delete static_cast<unsigned char*>(context);
}

mib_processing_status MIB_PROCESSING_CALL resetContext(mib_processing_context context,
                                                       char*, size_t) {
    return context ? MIB_PROCESSING_STATUS_OK : MIB_PROCESSING_STATUS_INVALID_ARGUMENT;
}

mib_processing_status MIB_PROCESSING_CALL processMask(
    mib_processing_context,
    const mib_processing_image_view*,
    const mib_processing_image_view*,
    const mib_processing_kernel_config*,
    const mib_processing_roi*,
    mib_processing_mutable_image_view*,
    char* error,
    size_t errorCapacity) {
    try {
        throw std::runtime_error("injected fixture exception");
    } catch (const std::exception& exception) {
        writeError(error, errorCapacity, exception.what());
        return MIB_PROCESSING_STATUS_INTERNAL_ERROR;
    } catch (...) {
        writeError(error, errorCapacity, "unknown fixture exception");
        return MIB_PROCESSING_STATUS_INTERNAL_ERROR;
    }
}

mib_processing_status MIB_PROCESSING_CALL isEmpty(
    mib_processing_context,
    const mib_processing_image_view*,
    const mib_processing_image_view*,
    const mib_processing_kernel_config*,
    const mib_processing_roi*,
    uint8_t* output,
    char*,
    size_t) {
    if (!output) return MIB_PROCESSING_STATUS_INVALID_ARGUMENT;
    *output = 1u;
    return MIB_PROCESSING_STATUS_OK;
}

mib_processing_status MIB_PROCESSING_CALL selfTest(char*, size_t) {
    return MIB_PROCESSING_STATUS_OK;
}

} // namespace

MIB_PROCESSING_EXPORT mib_processing_status MIB_PROCESSING_CALL mib_processing_get_api(
    uint32_t requestedEngineAbi,
    uint32_t hostApiStructSize,
    mib_processing_api* output,
    char* error,
    size_t errorCapacity) {
    if (requestedEngineAbi != MIB_PROCESSING_ENGINE_ABI_VERSION) {
        writeError(error, errorCapacity, "fixture ABI mismatch");
        return MIB_PROCESSING_STATUS_ABI_MISMATCH;
    }
    if (!output || hostApiStructSize < sizeof(*output)) {
        writeError(error, errorCapacity, "fixture host API table is too small");
        return MIB_PROCESSING_STATUS_BUFFER_TOO_SMALL;
    }
    mib_processing_api api{};
    api.struct_size = sizeof(api);
    api.engine_abi_version = MIB_PROCESSING_ENGINE_ABI_VERSION;
    api.descriptor = descriptor;
    api.create_context = createContext;
    api.destroy_context = destroyContext;
    api.reset_context = resetContext;
    api.process_mask = processMask;
    api.is_empty = isEmpty;
    api.self_test = selfTest;
    *output = api;
    return MIB_PROCESSING_STATUS_OK;
}

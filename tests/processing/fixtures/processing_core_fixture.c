#include "backend/processing/ProcessingCoreAbi.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifndef MIB_PROCESSING_CORE_VERSION
#  define MIB_PROCESSING_CORE_VERSION "0.1.0"
#endif
#ifndef MIB_PROCESSING_FIXTURE_MODE
#  define MIB_PROCESSING_FIXTURE_MODE 0
#endif

#define MIB_FIXTURE_STRINGIFY_INNER(value) #value
#define MIB_FIXTURE_STRINGIFY(value) MIB_FIXTURE_STRINGIFY_INNER(value)

#if defined(_WIN32) && defined(__x86_64__)
#  define MIB_FIXTURE_PLATFORM "windows-x86_64"
#elif defined(__linux__) && defined(__x86_64__)
#  define MIB_FIXTURE_PLATFORM "linux-x86_64"
#elif defined(__linux__) && defined(__aarch64__)
#  define MIB_FIXTURE_PLATFORM "linux-aarch64"
#elif defined(__APPLE__) && defined(__aarch64__)
#  define MIB_FIXTURE_PLATFORM "macos-aarch64"
#elif defined(__APPLE__) && defined(__x86_64__)
#  define MIB_FIXTURE_PLATFORM "macos-x86_64"
#else
#  define MIB_FIXTURE_PLATFORM "unknown-platform"
#endif

#if defined(_MSC_VER)
#  define MIB_FIXTURE_RUNTIME_FINGERPRINT \
    "windows-x86_64-msvc" MIB_FIXTURE_STRINGIFY(_MSC_VER) "-md-cxx17"
#elif defined(__clang__)
#  define MIB_FIXTURE_RUNTIME_FINGERPRINT \
    MIB_FIXTURE_PLATFORM "-clang" MIB_FIXTURE_STRINGIFY(__clang_major__) "-cxx17"
#elif defined(__GNUC__)
#  define MIB_FIXTURE_RUNTIME_FINGERPRINT \
    MIB_FIXTURE_PLATFORM "-gcc" MIB_FIXTURE_STRINGIFY(__GNUC__) "-cxx17"
#else
#  define MIB_FIXTURE_RUNTIME_FINGERPRINT "unknown-platform-cxx17"
#endif

static void write_error(char* error, size_t capacity, const char* message) {
    if (error == NULL || capacity == 0u) return;
    if (message == NULL) message = "";
    strncpy(error, message, capacity - 1u);
    error[capacity - 1u] = '\0';
}

static const mib_processing_core_descriptor* MIB_PROCESSING_CALL fixture_descriptor(void) {
    static const mib_processing_core_descriptor descriptor = {
        sizeof(mib_processing_core_descriptor),
        MIB_PROCESSING_ENGINE_ABI_VERSION,
        MIB_PROCESSING_FIXTURE_MODE == 2 ? MIB_PROCESSING_CONTRACT_VERSION + 1u
                                         : MIB_PROCESSING_CONTRACT_VERSION,
        0u,
        MIB_PROCESSING_CORE_VERSION,
        "independent-c-fixture",
        MIB_FIXTURE_RUNTIME_FINGERPRINT,
        {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u}};
    return &descriptor;
}

static mib_processing_status MIB_PROCESSING_CALL fixture_create_context(
    mib_processing_context* output, char* error, size_t error_capacity) {
    if (output == NULL) {
        write_error(error, error_capacity, "missing output context");
        return MIB_PROCESSING_STATUS_INVALID_ARGUMENT;
    }
    *output = malloc(1u);
    if (*output == NULL) {
        write_error(error, error_capacity, "fixture allocation failed");
        return MIB_PROCESSING_STATUS_INTERNAL_ERROR;
    }
    return MIB_PROCESSING_STATUS_OK;
}

static void MIB_PROCESSING_CALL fixture_destroy_context(mib_processing_context context) {
    free(context);
}

static mib_processing_status MIB_PROCESSING_CALL fixture_reset_context(
    mib_processing_context context, char* error, size_t error_capacity) {
    if (context == NULL) {
        write_error(error, error_capacity, "missing fixture context");
        return MIB_PROCESSING_STATUS_INVALID_ARGUMENT;
    }
    return MIB_PROCESSING_STATUS_OK;
}

static int valid_image(const mib_processing_image_view* image) {
    uint64_t required;
    if (image == NULL || image->struct_size < sizeof(*image) || image->data == NULL ||
        image->width == 0u || image->height == 0u || image->stride_bytes < image->width) {
        return 0;
    }
    required = (uint64_t)(image->height - 1u) * image->stride_bytes + image->width;
    return required <= image->data_size_bytes;
}

static mib_processing_status MIB_PROCESSING_CALL fixture_process_mask(
    mib_processing_context context,
    const mib_processing_image_view* input,
    const mib_processing_image_view* background,
    const mib_processing_kernel_config* config,
    const mib_processing_roi* roi,
    mib_processing_mutable_image_view* output,
    char* error,
    size_t error_capacity) {
    uint32_t row;
    (void)background;
    (void)config;
    (void)roi;
    if (context == NULL || !valid_image(input) || output == NULL || output->data == NULL ||
        output->struct_size < sizeof(*output) || output->width != input->width ||
        output->height != input->height || output->stride_bytes < output->width ||
        ((uint64_t)(output->height - 1u) * output->stride_bytes + output->width) >
            output->data_size_bytes) {
        write_error(error, error_capacity, "invalid fixture image buffers");
        return MIB_PROCESSING_STATUS_INVALID_ARGUMENT;
    }
    for (row = 0u; row < input->height; ++row) {
        memcpy(output->data + (uint64_t)row * output->stride_bytes,
               input->data + (uint64_t)row * input->stride_bytes, input->width);
    }
    return MIB_PROCESSING_STATUS_OK;
}

static mib_processing_status MIB_PROCESSING_CALL fixture_is_empty(
    mib_processing_context context,
    const mib_processing_image_view* input,
    const mib_processing_image_view* background,
    const mib_processing_kernel_config* config,
    const mib_processing_roi* roi,
    uint8_t* output,
    char* error,
    size_t error_capacity) {
    uint32_t row;
    (void)background;
    (void)config;
    (void)roi;
    if (context == NULL || !valid_image(input) || output == NULL) {
        write_error(error, error_capacity, "invalid fixture empty input");
        return MIB_PROCESSING_STATUS_INVALID_ARGUMENT;
    }
    *output = 1u;
    for (row = 0u; row < input->height; ++row) {
        uint32_t column;
        for (column = 0u; column < input->width; ++column) {
            if (input->data[(uint64_t)row * input->stride_bytes + column] != 0u) {
                *output = 0u;
                return MIB_PROCESSING_STATUS_OK;
            }
        }
    }
    return MIB_PROCESSING_STATUS_OK;
}

static mib_processing_status MIB_PROCESSING_CALL fixture_self_test(
    char* error, size_t error_capacity) {
    (void)error;
    (void)error_capacity;
    return MIB_PROCESSING_STATUS_OK;
}

MIB_PROCESSING_EXPORT mib_processing_status MIB_PROCESSING_CALL mib_processing_get_api(
    uint32_t requested_engine_abi,
    uint32_t host_api_struct_size,
    mib_processing_api* output,
    char* error,
    size_t error_capacity) {
    mib_processing_api api;
    if (requested_engine_abi != MIB_PROCESSING_ENGINE_ABI_VERSION) {
        write_error(error, error_capacity, "fixture ABI mismatch");
        return MIB_PROCESSING_STATUS_ABI_MISMATCH;
    }
    if (output == NULL || host_api_struct_size < sizeof(mib_processing_api)) {
        write_error(error, error_capacity, "fixture host API table is too small");
        return MIB_PROCESSING_STATUS_BUFFER_TOO_SMALL;
    }
    memset(&api, 0, sizeof(api));
    api.struct_size = MIB_PROCESSING_FIXTURE_MODE == 1
                          ? (uint32_t)offsetof(mib_processing_api, process_mask)
                          : (uint32_t)sizeof(api);
    api.engine_abi_version = MIB_PROCESSING_ENGINE_ABI_VERSION;
    api.descriptor = fixture_descriptor;
    api.create_context = fixture_create_context;
    api.destroy_context = fixture_destroy_context;
    api.reset_context = fixture_reset_context;
    api.process_mask = MIB_PROCESSING_FIXTURE_MODE == 3 ? NULL : fixture_process_mask;
    api.is_empty = fixture_is_empty;
    api.self_test = fixture_self_test;
    *output = api;
    return MIB_PROCESSING_STATUS_OK;
}

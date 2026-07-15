#include "backend/processing/ProcessingCoreAbi.h"

#include <stddef.h>

_Static_assert(MIB_PROCESSING_ENGINE_ABI_VERSION == 1u, "unexpected engine ABI");
_Static_assert(offsetof(mib_processing_api, struct_size) == 0u,
               "API table must start with struct_size");
_Static_assert(offsetof(mib_processing_kernel_config, flags) == 24u,
               "configuration flag offset changed");
_Static_assert(sizeof(((mib_processing_kernel_config*)0)->reserved_u32) == 40u,
               "configuration reserve changed");
_Static_assert(sizeof(mib_processing_kernel_config) == 68u,
               "configuration ABI size changed");

int main(void) {
    mib_processing_api api = {0};
    mib_processing_image_view image = {0};
    api.struct_size = (uint32_t)sizeof(api);
    image.struct_size = (uint32_t)sizeof(image);
    return (api.struct_size > image.struct_size &&
            MIB_PROCESSING_GET_API_SYMBOL[0] == 'm')
               ? 0
               : 1;
}

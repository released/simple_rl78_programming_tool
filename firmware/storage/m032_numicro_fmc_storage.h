#ifndef M032_NUMICRO_FMC_STORAGE_H
#define M032_NUMICRO_FMC_STORAGE_H

#include <stdint.h>

#include "NuMicro.h"
#include "m032_package_store.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct M032NumicroFmcStorageTag
{
    uint32_t base_address;
    uint32_t capacity;
    M032StorageBackend backend;
} M032NumicroFmcStorage;

uint32_t M032NumicroFmcStorage_Init(M032NumicroFmcStorage *storage,
                                    uint32_t base_address,
                                    uint32_t capacity,
                                    uint32_t firmware_end_address,
                                    uint32_t aprom_size);

#ifdef __cplusplus
}
#endif

#endif

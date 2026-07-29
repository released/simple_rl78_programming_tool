#ifndef NULINK2ME_SPIM_STORAGE_H
#define NULINK2ME_SPIM_STORAGE_H

#include <stdint.h>

#include "m032_package_store.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NULINK2ME_W25Q16_CAPACITY (2ul * 1024ul * 1024ul)
#define NULINK2ME_W25Q16_ERASE_SIZE 4096u
#define NULINK2ME_W25Q16_PAGE_SIZE 256u

typedef struct Nulink2MeSpimStorageTag
{
    M032StorageBackend backend;
    uint8_t jedec_id[3];
    uint8_t initialized;
} Nulink2MeSpimStorage;

uint32_t Nulink2MeSpimStorage_Init(Nulink2MeSpimStorage *storage);

#ifdef __cplusplus
}
#endif

#endif

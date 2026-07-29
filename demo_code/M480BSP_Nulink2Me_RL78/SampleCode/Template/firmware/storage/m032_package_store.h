#ifndef M032_PACKAGE_STORE_H
#define M032_PACKAGE_STORE_H

#include <stdint.h>

#include "programmer_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define M032_PACKAGE_STORE_RECORD_SIZE 128u

typedef struct M032StorageBackendTag
{
    void *context;
    uint32_t capacity;
    uint32_t erase_size;
    uint32_t (*read)(void *context, uint32_t offset, uint8_t *data, uint32_t length);
    uint32_t (*erase)(void *context, uint32_t offset, uint32_t length);
    uint32_t (*write)(void *context, uint32_t offset, const uint8_t *data,
                      uint32_t length);
    uint32_t (*sync)(void *context);
} M032StorageBackend;

typedef struct M032PackageStoreTag
{
    M032StorageBackend backend;
    uint32_t slot_offset;
    uint32_t slot_capacity;
    uint32_t journal_generation;
    uint32_t active_generation;
    uint32_t active_size;
    uint32_t mode_generation;
    uint32_t candidate_size;
    uint32_t candidate_crc32;
    uint32_t candidate_flags;
    uint32_t transfer_offset;
    uint16_t transfer_id;
    uint16_t next_transfer_id;
    uint8_t active_uuid[16];
    uint8_t candidate_uuid[16];
    uint8_t candidate_sha256[32];
    uint8_t active_valid;
    uint8_t candidate_valid;
    uint8_t transfer_active;
    uint8_t operating_mode;
    uint8_t autorun_enabled;
} M032PackageStore;

uint32_t M032PackageStore_Init(M032PackageStore *store,
                               const M032StorageBackend *backend);
void M032PackageStore_GetStorageOps(M032PackageStore *store,
                                    ProgrammerStorageOps *ops);
void M032PackageStore_GetModeOps(M032PackageStore *store,
                                 ProgrammerModeOps *ops);
uint32_t M032PackageStore_ReadActive(M032PackageStore *store, uint32_t offset,
                                     uint8_t *data, uint32_t length);
uint32_t M032PackageStore_GetActiveSize(const M032PackageStore *store,
                                        uint32_t *size);

#ifdef __cplusplus
}
#endif

#endif

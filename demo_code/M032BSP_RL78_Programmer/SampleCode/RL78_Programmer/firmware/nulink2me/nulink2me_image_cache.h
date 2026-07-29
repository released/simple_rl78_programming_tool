#ifndef NULINK2ME_IMAGE_CACHE_H
#define NULINK2ME_IMAGE_CACHE_H

#include <stdint.h>

#include "programmer_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NULINK2ME_IMAGE_CACHE_CAPACITY (64ul * 1024ul)

typedef uint32_t (*Nulink2MeImageSizeResolver)(
    const uint8_t target_profile_uuid[16], uint32_t *image_size);

typedef struct Nulink2MeImageCacheTag
{
    ProgrammerStorageOps backing;
    ProgrammerImageOps source_image;
    uint8_t *buffer;
    uint32_t capacity;
    uint32_t image_size;
    uint32_t image_crc32;
    Nulink2MeImageSizeResolver resolve_size;
    uint8_t package_uuid[16];
    uint8_t target_profile_uuid[16];
    uint8_t valid;
} Nulink2MeImageCache;

uint32_t Nulink2MeImageCache_Init(
    Nulink2MeImageCache *cache,
    const ProgrammerStorageOps *backing,
    uint8_t *buffer,
    uint32_t capacity,
    Nulink2MeImageSizeResolver resolve_size);
void Nulink2MeImageCache_GetStorageOps(
    Nulink2MeImageCache *cache,
    ProgrammerStorageOps *storage);
void Nulink2MeImageCache_Invalidate(Nulink2MeImageCache *cache);
uint32_t Nulink2MeImageCache_PreloadActive(Nulink2MeImageCache *cache);

#ifdef __cplusplus
}
#endif

#endif

#include "nulink2me_image_cache.h"

#include <string.h>

#include "rl78_programmer/integrity.h"
#include "rl78_programmer/plp_contract.h"

static uint32_t CachedReadTarget(void *context, uint32_t address,
                                 uint8_t *data, uint32_t length)
{
    Nulink2MeImageCache *cache;
    cache = (Nulink2MeImageCache *)context;
    if (cache == 0 || cache->valid == 0u || data == 0 || length == 0u ||
        address > cache->image_size ||
        length > cache->image_size - address)
    {
        return PLP_ERR_IMAGE_ADDRESS;
    }
    memcpy(data, cache->buffer + address, length);
    return PLP_OK;
}

static uint8_t CachedHasRegion(void *context, uint16_t section_type)
{
    Nulink2MeImageCache *cache;
    cache = (Nulink2MeImageCache *)context;
    if (cache == 0 || cache->valid == 0u ||
        cache->source_image.has_region == 0)
    {
        return 0u;
    }
    return cache->source_image.has_region(cache->source_image.context,
                                           section_type);
}

void Nulink2MeImageCache_Invalidate(Nulink2MeImageCache *cache)
{
    if (cache == 0)
    {
        return;
    }
    cache->valid = 0u;
    cache->image_size = 0u;
    cache->image_crc32 = 0u;
    memset(cache->package_uuid, 0, sizeof(cache->package_uuid));
    memset(cache->target_profile_uuid, 0,
           sizeof(cache->target_profile_uuid));
    memset(&cache->source_image, 0, sizeof(cache->source_image));
}

static uint32_t CacheBegin(void *context, const uint8_t package_uuid[16],
                           uint32_t total_size, uint32_t package_crc32,
                           const uint8_t hash_prefix[16],
                           uint16_t *transfer_id,
                           uint32_t *accepted_offset)
{
    Nulink2MeImageCache *cache;
    cache = (Nulink2MeImageCache *)context;
    if (cache == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    Nulink2MeImageCache_Invalidate(cache);
    return cache->backing.begin(cache->backing.context, package_uuid,
                                total_size, package_crc32, hash_prefix,
                                transfer_id, accepted_offset);
}

static uint32_t CacheWrite(void *context, uint16_t transfer_id,
                           uint32_t offset, const uint8_t *data,
                           uint8_t length, uint32_t *accepted_offset)
{
    Nulink2MeImageCache *cache;
    cache = (Nulink2MeImageCache *)context;
    if (cache == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    Nulink2MeImageCache_Invalidate(cache);
    return cache->backing.write(cache->backing.context, transfer_id, offset,
                                data, length, accepted_offset);
}

static uint32_t CacheFinalize(void *context, uint16_t transfer_id,
                              uint32_t total_size,
                              const uint8_t package_sha256[32],
                              uint32_t package_crc32)
{
    Nulink2MeImageCache *cache;
    cache = (Nulink2MeImageCache *)context;
    if (cache == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    Nulink2MeImageCache_Invalidate(cache);
    return cache->backing.finalize(cache->backing.context, transfer_id,
                                   total_size, package_sha256,
                                   package_crc32);
}

static uint32_t CacheActivate(void *context,
                              const uint8_t package_uuid[16],
                              uint32_t expected_generation,
                              uint32_t *new_generation)
{
    Nulink2MeImageCache *cache;
    cache = (Nulink2MeImageCache *)context;
    if (cache == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    Nulink2MeImageCache_Invalidate(cache);
    return cache->backing.activate(cache->backing.context, package_uuid,
                                   expected_generation, new_generation);
}

static uint32_t CacheGetActive(void *context, uint8_t package_uuid[16],
                               uint32_t *generation)
{
    Nulink2MeImageCache *cache;
    cache = (Nulink2MeImageCache *)context;
    if (cache == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    return cache->backing.get_active(cache->backing.context, package_uuid,
                                     generation);
}

static uint32_t CacheOpenImage(void *context, uint8_t source_kind,
                               const uint8_t package_uuid[16],
                               ProgrammerImageOps *image)
{
    Nulink2MeImageCache *cache;
    ProgrammerImageOps source;
    uint32_t image_size;
    uint32_t status;
    uint32_t crc32;
    cache = (Nulink2MeImageCache *)context;
    if (cache == 0 || package_uuid == 0 || image == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    memset(&source, 0, sizeof(source));
    status = cache->backing.open_image(cache->backing.context, source_kind,
                                       package_uuid, &source);
    if (status != PLP_OK)
    {
        Nulink2MeImageCache_Invalidate(cache);
        return status;
    }
    if (source.read_target == 0 || source.has_region == 0)
    {
        Nulink2MeImageCache_Invalidate(cache);
        return PLP_ERR_CONFIG_VALIDATION;
    }
    status = cache->resolve_size(source.target_profile_uuid, &image_size);
    if (status != PLP_OK)
    {
        Nulink2MeImageCache_Invalidate(cache);
        return status;
    }
    if (image_size == 0u || image_size > cache->capacity)
    {
        Nulink2MeImageCache_Invalidate(cache);
        return PLP_ERR_STORAGE_CAPACITY;
    }
    if (cache->valid != 0u && cache->image_size == image_size &&
        memcmp(cache->package_uuid, package_uuid, 16u) == 0 &&
        memcmp(cache->target_profile_uuid, source.target_profile_uuid,
               16u) == 0)
    {
        crc32 = Rpp_Crc32IsoHdlc(cache->buffer, cache->image_size);
        if (crc32 == cache->image_crc32)
        {
            memcpy(&cache->source_image, &source, sizeof(source));
            memset(image, 0, sizeof(*image));
            image->context = cache;
            memcpy(image->target_profile_uuid, source.target_profile_uuid,
                   16u);
            image->read_target = CachedReadTarget;
            image->has_region = CachedHasRegion;
            return PLP_OK;
        }
    }
    Nulink2MeImageCache_Invalidate(cache);
    status = source.read_target(source.context, 0u, cache->buffer, image_size);
    if (status != PLP_OK)
    {
        return status;
    }
    cache->image_size = image_size;
    cache->image_crc32 = Rpp_Crc32IsoHdlc(cache->buffer, image_size);
    memcpy(cache->package_uuid, package_uuid, 16u);
    memcpy(cache->target_profile_uuid, source.target_profile_uuid, 16u);
    memcpy(&cache->source_image, &source, sizeof(source));
    cache->valid = 1u;
    memset(image, 0, sizeof(*image));
    image->context = cache;
    memcpy(image->target_profile_uuid, source.target_profile_uuid, 16u);
    image->read_target = CachedReadTarget;
    image->has_region = CachedHasRegion;
    return PLP_OK;
}

uint32_t Nulink2MeImageCache_Init(
    Nulink2MeImageCache *cache,
    const ProgrammerStorageOps *backing,
    uint8_t *buffer,
    uint32_t capacity,
    Nulink2MeImageSizeResolver resolve_size)
{
    if (cache == 0 || backing == 0 || buffer == 0 || capacity == 0u ||
        resolve_size == 0 || backing->begin == 0 || backing->write == 0 ||
        backing->finalize == 0 || backing->activate == 0 ||
        backing->get_active == 0 || backing->open_image == 0)
    {
        return PLP_ERR_CONFIG_VALIDATION;
    }
    memset(cache, 0, sizeof(*cache));
    memcpy(&cache->backing, backing, sizeof(*backing));
    cache->buffer = buffer;
    cache->capacity = capacity;
    cache->resolve_size = resolve_size;
    return PLP_OK;
}

void Nulink2MeImageCache_GetStorageOps(
    Nulink2MeImageCache *cache,
    ProgrammerStorageOps *storage)
{
    if (cache == 0 || storage == 0)
    {
        return;
    }
    memset(storage, 0, sizeof(*storage));
    storage->context = cache;
    storage->begin = CacheBegin;
    storage->write = CacheWrite;
    storage->finalize = CacheFinalize;
    storage->activate = CacheActivate;
    storage->get_active = CacheGetActive;
    storage->open_image = CacheOpenImage;
}

uint32_t Nulink2MeImageCache_PreloadActive(Nulink2MeImageCache *cache)
{
    ProgrammerImageOps image;
    uint8_t package_uuid[16];
    uint32_t generation;
    uint32_t status;
    if (cache == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    status = cache->backing.get_active(cache->backing.context, package_uuid,
                                       &generation);
    if (status != PLP_OK)
    {
        return status;
    }
    (void)generation;
    memset(&image, 0, sizeof(image));
    return CacheOpenImage(cache, 1u, package_uuid, &image);
}

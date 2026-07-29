#include "m032_package_store.h"

#include <string.h>

#include "rl78_programmer/integrity.h"
#include "rl78_programmer/oip_format.h"
#include "rl78_programmer/plp_frame.h"

#define STORE_MAGIC_0 0x4Du
#define STORE_MAGIC_1 0x50u
#define STORE_MAGIC_2 0x53u
#define STORE_MAGIC_3 0x31u
#define STORE_VERSION 2u
#define STORE_RECORD_CRC_OFFSET 104u

static uint8_t UuidIsZero(const uint8_t uuid[16])
{
    uint8_t index;
    for (index = 0u; index < 16u; ++index)
    {
        if (uuid[index] != 0u)
        {
            return 0u;
        }
    }
    return 1u;
}

static uint32_t RoundUp(uint32_t value, uint32_t unit, uint32_t *rounded)
{
    uint32_t remainder;
    if (unit == 0u || rounded == 0)
    {
        return PLP_ERR_CONFIG_VALIDATION;
    }
    remainder = value % unit;
    if (remainder == 0u)
    {
        *rounded = value;
        return PLP_OK;
    }
    if (value > 0xFFFFFFFFul - (unit - remainder))
    {
        return PLP_ERR_STORAGE_CAPACITY;
    }
    *rounded = value + unit - remainder;
    return PLP_OK;
}

static uint32_t ValidateRecord(const uint8_t record[M032_PACKAGE_STORE_RECORD_SIZE])
{
    uint8_t copy[M032_PACKAGE_STORE_RECORD_SIZE];
    uint32_t expected;
    uint32_t actual;
    if (record[0] != STORE_MAGIC_0 || record[1] != STORE_MAGIC_1 ||
        record[2] != STORE_MAGIC_2 || record[3] != STORE_MAGIC_3 ||
        Rpp_ReadLe16(record + 4u) != STORE_VERSION)
    {
        return PLP_ERR_CONFIG_SCHEMA;
    }
    memcpy(copy, record, sizeof(copy));
    expected = Rpp_ReadLe32(copy + STORE_RECORD_CRC_OFFSET);
    memset(copy + STORE_RECORD_CRC_OFFSET, 0, 4u);
    actual = Rpp_Crc32IsoHdlc(copy, sizeof(copy));
    return actual == expected ? PLP_OK : PLP_ERR_INTEGRITY_CRC;
}

static void DecodeRecord(M032PackageStore *store,
                         const uint8_t record[M032_PACKAGE_STORE_RECORD_SIZE])
{
    store->journal_generation = Rpp_ReadLe32(record + 8u);
    store->active_valid = record[12];
    store->candidate_valid = record[14];
    memcpy(store->active_uuid, record + 16u, 16u);
    memcpy(store->candidate_uuid, record + 32u, 16u);
    store->candidate_size = Rpp_ReadLe32(record + 48u);
    store->candidate_crc32 = Rpp_ReadLe32(record + 52u);
    memcpy(store->candidate_sha256, record + 56u, 32u);
    store->active_generation = Rpp_ReadLe32(record + 88u);
    store->mode_generation = Rpp_ReadLe32(record + 92u);
    store->operating_mode = record[96];
    store->autorun_enabled = record[97];
    store->candidate_flags = Rpp_ReadLe32(record + 100u);
    store->active_size = Rpp_ReadLe32(record + 108u);
}

static void EncodeRecord(const M032PackageStore *store,
                         uint32_t generation,
                         uint8_t record[M032_PACKAGE_STORE_RECORD_SIZE])
{
    uint32_t crc;
    memset(record, 0, M032_PACKAGE_STORE_RECORD_SIZE);
    record[0] = STORE_MAGIC_0;
    record[1] = STORE_MAGIC_1;
    record[2] = STORE_MAGIC_2;
    record[3] = STORE_MAGIC_3;
    Rpp_WriteLe16(record + 4u, STORE_VERSION);
    Rpp_WriteLe32(record + 8u, generation);
    record[12] = store->active_valid;
    record[14] = store->candidate_valid;
    memcpy(record + 16u, store->active_uuid, 16u);
    memcpy(record + 32u, store->candidate_uuid, 16u);
    Rpp_WriteLe32(record + 48u, store->candidate_size);
    Rpp_WriteLe32(record + 52u, store->candidate_crc32);
    memcpy(record + 56u, store->candidate_sha256, 32u);
    Rpp_WriteLe32(record + 88u, store->active_generation);
    Rpp_WriteLe32(record + 92u, store->mode_generation);
    record[96] = store->operating_mode;
    record[97] = store->autorun_enabled;
    Rpp_WriteLe32(record + 100u, store->candidate_flags);
    Rpp_WriteLe32(record + 108u, store->active_size);
    crc = Rpp_Crc32IsoHdlc(record, M032_PACKAGE_STORE_RECORD_SIZE);
    Rpp_WriteLe32(record + STORE_RECORD_CRC_OFFSET, crc);
}

static uint32_t CommitRecord(M032PackageStore *store)
{
    uint8_t record[M032_PACKAGE_STORE_RECORD_SIZE];
    uint32_t generation;
    uint32_t page;
    uint32_t status;
    generation = store->journal_generation + 1u;
    page = (generation & 1u) * store->backend.erase_size;
    EncodeRecord(store, generation, record);
    status = store->backend.erase(store->backend.context, page,
                                  store->backend.erase_size);
    if (status != PLP_OK)
    {
        return status;
    }
    status = store->backend.write(store->backend.context, page, record,
                                  M032_PACKAGE_STORE_RECORD_SIZE);
    if (status != PLP_OK)
    {
        return status;
    }
    if (store->backend.sync != 0)
    {
        status = store->backend.sync(store->backend.context);
        if (status != PLP_OK)
        {
            return status;
        }
    }
    store->journal_generation = generation;
    return PLP_OK;
}

static uint32_t StoreBegin(void *context, const uint8_t package_uuid[16],
                           uint32_t total_size, uint32_t package_crc32,
                           const uint8_t hash_prefix[16], uint16_t *transfer_id,
                           uint32_t *accepted_offset)
{
    M032PackageStore *store;
    uint32_t erase_length;
    uint32_t status;
    (void)hash_prefix;
    store = (M032PackageStore *)context;
    if (store == 0 || package_uuid == 0 || transfer_id == 0 ||
        accepted_offset == 0 || total_size < RPP_OIP_HEADER_SIZE ||
        total_size > store->slot_capacity)
    {
        return PLP_ERR_STORAGE_CAPACITY;
    }
    if (store->transfer_active != 0u)
    {
        return PLP_ERR_PROGRAM_BUSY;
    }
    status = RoundUp(total_size, store->backend.erase_size, &erase_length);
    if (status != PLP_OK || erase_length > store->slot_capacity)
    {
        return PLP_ERR_STORAGE_CAPACITY;
    }
    if (store->active_valid != 0u || store->candidate_valid != 0u)
    {
        store->active_valid = 0u;
        store->active_size = 0u;
        memset(store->active_uuid, 0, sizeof(store->active_uuid));
        store->candidate_valid = 0u;
        store->candidate_size = 0u;
        store->candidate_crc32 = 0u;
        store->candidate_flags = 0u;
        memset(store->candidate_uuid, 0, sizeof(store->candidate_uuid));
        memset(store->candidate_sha256, 0, sizeof(store->candidate_sha256));
        status = CommitRecord(store);
        if (status != PLP_OK)
        {
            return status;
        }
    }
    status = store->backend.erase(store->backend.context,
                                  store->slot_offset,
                                  erase_length);
    if (status != PLP_OK)
    {
        return status;
    }
    store->candidate_valid = 0u;
    memcpy(store->candidate_uuid, package_uuid, 16u);
    memset(store->candidate_sha256, 0, 32u);
    store->candidate_size = total_size;
    store->candidate_crc32 = package_crc32;
    store->candidate_flags = 0u;
    store->transfer_offset = 0u;
    store->next_transfer_id++;
    if (store->next_transfer_id == 0u)
    {
        store->next_transfer_id = 1u;
    }
    store->transfer_id = store->next_transfer_id;
    store->transfer_active = 1u;
    *transfer_id = store->transfer_id;
    *accepted_offset = 0u;
    return PLP_OK;
}

static uint32_t StoreWrite(void *context, uint16_t transfer_id,
                           uint32_t offset, const uint8_t *data, uint8_t length,
                           uint32_t *accepted_offset)
{
    M032PackageStore *store;
    uint32_t status;
    store = (M032PackageStore *)context;
    if (store == 0 || data == 0 || accepted_offset == 0 ||
        store->transfer_active == 0u || transfer_id != store->transfer_id ||
        offset != store->transfer_offset ||
        (uint32_t)length > store->candidate_size - store->transfer_offset)
    {
        return PLP_ERR_STORAGE_OFFSET;
    }
    status = store->backend.write(store->backend.context,
                                  store->slot_offset + offset,
                                  data, length);
    if (status != PLP_OK)
    {
        return status;
    }
    store->transfer_offset += length;
    *accepted_offset = store->transfer_offset;
    return PLP_OK;
}

static uint32_t ValidateStoredHeader(M032PackageStore *store, uint32_t *flags)
{
    uint8_t header[RPP_OIP_HEADER_SIZE];
    uint8_t copy[RPP_OIP_HEADER_SIZE];
    uint32_t total;
    uint32_t manifest_offset;
    uint32_t manifest_size;
    uint32_t descriptor_offset;
    uint32_t payload_offset;
    uint32_t payload_size;
    uint32_t descriptor_bytes;
    uint32_t expected_header_crc;
    uint32_t status;
    uint16_t section_count;
    status = store->backend.read(store->backend.context,
                                 store->slot_offset,
                                 header, sizeof(header));
    if (status != PLP_OK)
    {
        return status;
    }
    if (header[0] != RPP_OIP_MAGIC_0 || header[1] != RPP_OIP_MAGIC_1 ||
        header[2] != RPP_OIP_MAGIC_2 || header[3] != RPP_OIP_MAGIC_3 ||
        Rpp_ReadLe16(header + 4u) != RPP_OIP_HEADER_SIZE ||
        header[6] != RPP_OIP_VERSION_MAJOR ||
        memcmp(header + 40u, store->candidate_uuid, 16u) != 0)
    {
        return PLP_ERR_IMAGE_FORMAT;
    }
    total = Rpp_ReadLe32(header + 12u);
    manifest_offset = Rpp_ReadLe32(header + 16u);
    manifest_size = Rpp_ReadLe32(header + 20u);
    descriptor_offset = Rpp_ReadLe32(header + 24u);
    section_count = Rpp_ReadLe16(header + 28u);
    payload_offset = Rpp_ReadLe32(header + 32u);
    payload_size = Rpp_ReadLe32(header + 36u);
    if (total != store->candidate_size ||
        manifest_offset > total || manifest_size > total - manifest_offset ||
        descriptor_offset > total ||
        section_count > (total - descriptor_offset) / RPP_OIP_DESCRIPTOR_SIZE ||
        payload_offset > total || payload_size > total - payload_offset)
    {
        return PLP_ERR_IMAGE_FORMAT;
    }
    descriptor_bytes = (uint32_t)section_count * RPP_OIP_DESCRIPTOR_SIZE;
    if (descriptor_bytes > total - descriptor_offset)
    {
        return PLP_ERR_IMAGE_FORMAT;
    }
    memcpy(copy, header, sizeof(copy));
    expected_header_crc = Rpp_ReadLe32(copy + RPP_OIP_HEADER_CRC_OFFSET);
    memset(copy + RPP_OIP_HEADER_CRC_OFFSET, 0, 8u);
    if (Rpp_Crc32IsoHdlc(copy, sizeof(copy)) != expected_header_crc)
    {
        return PLP_ERR_INTEGRITY_CRC;
    }
    *flags = Rpp_ReadLe32(header + 8u);
    return PLP_OK;
}

static uint32_t VerifyStoredPackage(M032PackageStore *store,
                                    const uint8_t expected_sha256[32],
                                    uint32_t expected_crc32)
{
    uint8_t buffer[64];
    uint8_t digest[32];
    RppSha256Context sha;
    RppCrc32Context crc;
    uint32_t offset;
    uint32_t length;
    uint32_t index;
    uint32_t status;
    Rpp_Sha256Init(&sha);
    Rpp_Crc32Init(&crc);
    offset = 0u;
    while (offset < store->candidate_size)
    {
        length = store->candidate_size - offset;
        if (length > sizeof(buffer))
        {
            length = sizeof(buffer);
        }
        status = store->backend.read(store->backend.context,
                                     store->slot_offset + offset,
                                     buffer, length);
        if (status != PLP_OK)
        {
            return status;
        }
        Rpp_Sha256Update(&sha, buffer, length);
        for (index = 0u; index < length; ++index)
        {
            if (offset + index >= RPP_OIP_HEADER_CRC_OFFSET &&
                offset + index < RPP_OIP_PACKAGE_CRC_OFFSET + 4u)
            {
                buffer[index] = 0u;
            }
        }
        Rpp_Crc32Update(&crc, buffer, length);
        offset += length;
    }
    Rpp_Sha256Final(&sha, digest);
    if (memcmp(digest, expected_sha256, 32u) != 0)
    {
        return PLP_ERR_INTEGRITY_HASH;
    }
    if (Rpp_Crc32Final(&crc) != expected_crc32)
    {
        return PLP_ERR_INTEGRITY_CRC;
    }
    return PLP_OK;
}

static uint32_t StoreFinalize(void *context, uint16_t transfer_id,
                              uint32_t total_size,
                              const uint8_t package_sha256[32],
                              uint32_t package_crc32)
{
    M032PackageStore *store;
    uint32_t flags;
    uint32_t status;
    store = (M032PackageStore *)context;
    if (store == 0 || package_sha256 == 0 || store->transfer_active == 0u ||
        transfer_id != store->transfer_id || total_size != store->candidate_size ||
        store->transfer_offset != store->candidate_size ||
        package_crc32 != store->candidate_crc32)
    {
        return PLP_ERR_STORAGE_OFFSET;
    }
    status = ValidateStoredHeader(store, &flags);
    if (status == PLP_OK)
    {
        status = VerifyStoredPackage(store, package_sha256, package_crc32);
    }
    if (status != PLP_OK)
    {
        store->transfer_active = 0u;
        return status;
    }
    memcpy(store->candidate_sha256, package_sha256, 32u);
    store->candidate_flags = flags;
    store->candidate_valid = 1u;
    store->transfer_active = 0u;
    return CommitRecord(store);
}

static uint32_t StoreActivate(void *context, const uint8_t package_uuid[16],
                              uint32_t expected_generation,
                              uint32_t *new_generation)
{
    M032PackageStore *store;
    uint32_t status;
    store = (M032PackageStore *)context;
    if (store == 0 || package_uuid == 0 || new_generation == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    if (store->candidate_valid == 0u ||
        memcmp(package_uuid, store->candidate_uuid, 16u) != 0)
    {
        return PLP_ERR_STORAGE_NO_IMAGE;
    }
    if ((store->candidate_flags & RPP_OIP_FLAG_ALLOW_OFFLINE) == 0u)
    {
        return PLP_ERR_IMAGE_INCOMPATIBLE;
    }
    if (expected_generation != 0u && expected_generation != store->active_generation)
    {
        return PLP_ERR_CONFIG_PERSIST;
    }
    store->active_valid = 1u;
    memcpy(store->active_uuid, store->candidate_uuid, 16u);
    store->active_generation++;
    store->active_size = store->candidate_size;
    status = CommitRecord(store);
    if (status != PLP_OK)
    {
        return status;
    }
    *new_generation = store->active_generation;
    return PLP_OK;
}

static uint32_t StoreGetActive(void *context, uint8_t package_uuid[16],
                               uint32_t *generation)
{
    M032PackageStore *store;
    store = (M032PackageStore *)context;
    if (store == 0 || package_uuid == 0 || generation == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    if (store->active_valid == 0u || UuidIsZero(store->active_uuid) != 0u)
    {
        return PLP_ERR_STORAGE_NO_IMAGE;
    }
    memcpy(package_uuid, store->active_uuid, 16u);
    *generation = store->active_generation;
    return PLP_OK;
}

static uint32_t ModeLoad(void *context, uint8_t *mode,
                         uint8_t *autorun_enabled, uint32_t *generation)
{
    M032PackageStore *store;
    store = (M032PackageStore *)context;
    if (store == 0 || mode == 0 || autorun_enabled == 0 || generation == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    *mode = store->operating_mode;
    *autorun_enabled = store->autorun_enabled;
    *generation = store->mode_generation;
    return PLP_OK;
}

static uint32_t ModeStore(void *context, uint8_t mode,
                          uint8_t autorun_enabled,
                          uint32_t expected_generation,
                          uint32_t *new_generation)
{
    M032PackageStore *store;
    uint32_t status;
    store = (M032PackageStore *)context;
    if (store == 0 || new_generation == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    if (expected_generation != store->mode_generation)
    {
        return PLP_ERR_CONFIG_PERSIST;
    }
    store->operating_mode = mode;
    store->autorun_enabled = autorun_enabled;
    store->mode_generation++;
    status = CommitRecord(store);
    if (status != PLP_OK)
    {
        return status;
    }
    *new_generation = store->mode_generation;
    return PLP_OK;
}

static uint32_t ImageReadTarget(void *context, uint32_t address,
                                uint8_t *data, uint32_t length)
{
    M032PackageStore *store;
    uint8_t header[RPP_OIP_HEADER_SIZE];
    uint8_t descriptor[RPP_OIP_DESCRIPTOR_SIZE];
    uint32_t request_end;
    uint32_t descriptor_offset;
    uint32_t section_address;
    uint32_t section_size;
    uint32_t section_end;
    uint32_t stored_size;
    uint32_t payload_offset;
    uint32_t copy_start;
    uint32_t copy_end;
    uint32_t copy_length;
    uint32_t stored_length;
    uint32_t section_relative;
    uint32_t destination_relative;
    uint32_t status;
    uint16_t section_count;
    uint16_t index;
    store = (M032PackageStore *)context;
    if (store == 0 || data == 0 || length == 0u ||
        address > 0xFFFFFFFFul - length)
    {
        return PLP_ERR_IMAGE_ADDRESS;
    }
    request_end = address + length;
    memset(data, 0xFF, length);
    status = store->backend.read(store->backend.context, store->slot_offset,
                                 header, sizeof(header));
    if (status != PLP_OK)
    {
        return status;
    }
    descriptor_offset = Rpp_ReadLe32(header + 24u);
    section_count = Rpp_ReadLe16(header + 28u);
    for (index = 0u; index < section_count; ++index)
    {
        status = store->backend.read(
            store->backend.context,
            store->slot_offset + descriptor_offset +
                ((uint32_t)index * RPP_OIP_DESCRIPTOR_SIZE),
            descriptor, sizeof(descriptor));
        if (status != PLP_OK)
        {
            return status;
        }
        section_address = Rpp_ReadLe32(descriptor + 8u);
        section_size = Rpp_ReadLe32(descriptor + 12u);
        stored_size = Rpp_ReadLe32(descriptor + 16u);
        payload_offset = Rpp_ReadLe32(descriptor + 20u);
        if (section_size == 0u ||
            section_address > 0xFFFFFFFFul - section_size)
        {
            return PLP_ERR_IMAGE_ADDRESS;
        }
        section_end = section_address + section_size;
        if (request_end <= section_address || address >= section_end)
        {
            continue;
        }
        copy_start = address > section_address ? address : section_address;
        copy_end = request_end < section_end ? request_end : section_end;
        copy_length = copy_end - copy_start;
        section_relative = copy_start - section_address;
        destination_relative = copy_start - address;
        memset(data + destination_relative, descriptor[31], copy_length);
        if (section_relative < stored_size)
        {
            stored_length = stored_size - section_relative;
            if (stored_length > copy_length)
            {
                stored_length = copy_length;
            }
            status = store->backend.read(
                store->backend.context,
                store->slot_offset + payload_offset + section_relative,
                data + destination_relative, stored_length);
            if (status != PLP_OK)
            {
                return status;
            }
        }
    }
    return PLP_OK;
}

static uint8_t ImageHasRegion(void *context, uint16_t section_type)
{
    M032PackageStore *store;
    uint8_t header[RPP_OIP_HEADER_SIZE];
    uint8_t descriptor[RPP_OIP_DESCRIPTOR_SIZE];
    uint32_t descriptor_offset;
    uint16_t section_count;
    uint16_t index;
    store = (M032PackageStore *)context;
    if (store == 0 ||
        store->backend.read(store->backend.context, store->slot_offset,
                            header, sizeof(header)) != PLP_OK)
    {
        return 0u;
    }
    descriptor_offset = Rpp_ReadLe32(header + 24u);
    section_count = Rpp_ReadLe16(header + 28u);
    for (index = 0u; index < section_count; ++index)
    {
        if (store->backend.read(
                store->backend.context,
                store->slot_offset + descriptor_offset +
                    ((uint32_t)index * RPP_OIP_DESCRIPTOR_SIZE),
                descriptor, sizeof(descriptor)) != PLP_OK)
        {
            return 0u;
        }
        if (Rpp_ReadLe16(descriptor) == section_type)
        {
            return 1u;
        }
    }
    return 0u;
}

static uint32_t StoreOpenImage(void *context, uint8_t source_kind,
                               const uint8_t package_uuid[16],
                               ProgrammerImageOps *image)
{
    M032PackageStore *store;
    uint8_t header[RPP_OIP_HEADER_SIZE];
    uint8_t available;
    uint32_t status;
    store = (M032PackageStore *)context;
    if (store == 0 || package_uuid == 0 || image == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    available = 0u;
    if (source_kind == 0u)
    {
        available = (store->candidate_valid != 0u &&
                     memcmp(store->candidate_uuid, package_uuid, 16u) == 0) ?
                        1u : 0u;
    }
    else if (source_kind == 1u)
    {
        available = (store->active_valid != 0u &&
                     memcmp(store->active_uuid, package_uuid, 16u) == 0) ?
                        1u : 0u;
    }
    else if (source_kind == 2u)
    {
        available = ((store->active_valid != 0u &&
                      memcmp(store->active_uuid, package_uuid, 16u) == 0) ||
                     (store->candidate_valid != 0u &&
                      memcmp(store->candidate_uuid, package_uuid, 16u) == 0)) ?
                        1u : 0u;
    }
    if (available == 0u)
    {
        return PLP_ERR_STORAGE_NO_IMAGE;
    }
    status = store->backend.read(store->backend.context, store->slot_offset,
                                 header, sizeof(header));
    if (status != PLP_OK)
    {
        return status;
    }
    if (memcmp(header + 40u, package_uuid, 16u) != 0)
    {
        return PLP_ERR_IMAGE_INCOMPATIBLE;
    }
    memset(image, 0, sizeof(*image));
    image->context = store;
    memcpy(image->target_profile_uuid, header + 88u, 16u);
    image->read_target = ImageReadTarget;
    image->has_region = ImageHasRegion;
    return PLP_OK;
}

uint32_t M032PackageStore_Init(M032PackageStore *store,
                               const M032StorageBackend *backend)
{
    uint8_t first[M032_PACKAGE_STORE_RECORD_SIZE];
    uint8_t second[M032_PACKAGE_STORE_RECORD_SIZE];
    uint32_t first_status;
    uint32_t second_status;
    uint32_t first_generation;
    uint32_t second_generation;
    uint32_t data_offset;
    uint32_t data_capacity;
    if (store == 0 || backend == 0 || backend->read == 0 ||
        backend->erase == 0 || backend->write == 0 ||
        backend->erase_size < M032_PACKAGE_STORE_RECORD_SIZE ||
        backend->capacity < backend->erase_size * 3u)
    {
        return PLP_ERR_CONFIG_VALIDATION;
    }
    memset(store, 0, sizeof(*store));
    memcpy(&store->backend, backend, sizeof(*backend));
    store->operating_mode = PLP_OPERATING_MODE_ONLINE_ONLY;
    store->next_transfer_id = 1u;
    data_offset = backend->erase_size * 2u;
    data_capacity = backend->capacity - data_offset;
    store->slot_capacity = (data_capacity / backend->erase_size) *
                           backend->erase_size;
    store->slot_offset = data_offset;
    if (store->slot_capacity < backend->erase_size)
    {
        return PLP_ERR_STORAGE_CAPACITY;
    }
    first_status = backend->read(backend->context, 0u, first, sizeof(first));
    second_status = backend->read(backend->context, backend->erase_size,
                                  second, sizeof(second));
    if (first_status == PLP_OK)
    {
        first_status = ValidateRecord(first);
    }
    if (second_status == PLP_OK)
    {
        second_status = ValidateRecord(second);
    }
    first_generation = first_status == PLP_OK ? Rpp_ReadLe32(first + 8u) : 0u;
    second_generation = second_status == PLP_OK ? Rpp_ReadLe32(second + 8u) : 0u;
    if (first_status == PLP_OK || second_status == PLP_OK)
    {
        DecodeRecord(store, second_generation > first_generation ? second : first);
        if (store->active_valid > 1u || store->candidate_valid > 1u ||
            store->operating_mode > PLP_OPERATING_MODE_OFFLINE_ARMED ||
            store->autorun_enabled > 1u ||
            store->candidate_size > store->slot_capacity ||
            store->active_size > store->slot_capacity)
        {
            return PLP_ERR_CONFIG_SCHEMA;
        }
    }
    return PLP_OK;
}

void M032PackageStore_GetStorageOps(M032PackageStore *store,
                                    ProgrammerStorageOps *ops)
{
    if (store == 0 || ops == 0)
    {
        return;
    }
    memset(ops, 0, sizeof(*ops));
    ops->context = store;
    ops->begin = StoreBegin;
    ops->write = StoreWrite;
    ops->finalize = StoreFinalize;
    ops->activate = StoreActivate;
    ops->get_active = StoreGetActive;
    ops->open_image = StoreOpenImage;
}

void M032PackageStore_GetModeOps(M032PackageStore *store,
                                 ProgrammerModeOps *ops)
{
    if (store == 0 || ops == 0)
    {
        return;
    }
    memset(ops, 0, sizeof(*ops));
    ops->context = store;
    ops->load = ModeLoad;
    ops->store = ModeStore;
}

uint32_t M032PackageStore_ReadActive(M032PackageStore *store, uint32_t offset,
                                     uint8_t *data, uint32_t length)
{
    if (store == 0 || data == 0 || store->active_valid == 0u ||
        offset > store->active_size || length > store->active_size - offset)
    {
        return PLP_ERR_STORAGE_OFFSET;
    }
    return store->backend.read(store->backend.context,
                               store->slot_offset + offset,
                               data, length);
}

uint32_t M032PackageStore_GetActiveSize(const M032PackageStore *store,
                                        uint32_t *size)
{
    if (store == 0 || size == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    if (store->active_valid == 0u)
    {
        return PLP_ERR_STORAGE_NO_IMAGE;
    }
    *size = store->active_size;
    return PLP_OK;
}

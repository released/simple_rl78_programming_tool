#include "rl78_programmer/oip_format.h"

#include <string.h>

#include "rl78_programmer/integrity.h"
#include "rl78_programmer/plp_contract.h"
#include "rl78_programmer/plp_frame.h"

static int Rpp_RangeValid(uint32_t offset, uint32_t size, uint32_t total)
{
    if (offset > total)
    {
        return 0;
    }
    if (size > (total - offset))
    {
        return 0;
    }
    return 1;
}

static uint32_t Rpp_Crc32PackageCanonical(const uint8_t *data, size_t size)
{
    uint32_t crc;
    uint32_t bit;
    size_t index;
    uint8_t value;

    crc = 0xFFFFFFFFul;
    for (index = 0u; index < size; ++index)
    {
        value = data[index];
        if ((index >= RPP_OIP_HEADER_CRC_OFFSET) && (index < (RPP_OIP_PACKAGE_CRC_OFFSET + 4u)))
        {
            value = 0u;
        }
        crc ^= (uint32_t)value;
        for (bit = 0u; bit < 8u; ++bit)
        {
            if ((crc & 1u) != 0u)
            {
                crc = (crc >> 1) ^ 0xEDB88320ul;
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    return crc ^ 0xFFFFFFFFul;
}

uint32_t Rpp_OipValidateBasic(const uint8_t *package_data,
                              size_t package_size,
                              RppOipInfo *info)
{
    uint8_t header_copy[RPP_OIP_HEADER_SIZE];
    uint8_t digest[32];
    uint32_t declared_size;
    uint32_t manifest_offset;
    uint32_t manifest_size;
    uint32_t descriptor_offset;
    uint32_t payload_offset;
    uint32_t payload_size;
    uint32_t signature_offset;
    uint32_t signature_size;
    uint32_t canonical_size;
    uint32_t expected_crc;
    uint32_t actual_crc;
    uint16_t section_count;
    uint16_t descriptor_size;
    uint16_t index;
    const uint8_t *descriptor;
    uint32_t section_offset;
    uint32_t section_size;

    if ((package_data == 0) || (info == 0))
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    memset(info, 0, sizeof(*info));
    if ((package_size < RPP_OIP_HEADER_SIZE) || (package_size > 0xFFFFFFFFul))
    {
        return PLP_ERR_IMAGE_FORMAT;
    }
    if ((package_data[0] != RPP_OIP_MAGIC_0) ||
        (package_data[1] != RPP_OIP_MAGIC_1) ||
        (package_data[2] != RPP_OIP_MAGIC_2) ||
        (package_data[3] != RPP_OIP_MAGIC_3))
    {
        return PLP_ERR_IMAGE_FORMAT;
    }
    if ((Rpp_ReadLe16(package_data + 4) != RPP_OIP_HEADER_SIZE) ||
        (package_data[6] != RPP_OIP_VERSION_MAJOR) ||
        (package_data[7] > RPP_OIP_VERSION_MINOR))
    {
        return PLP_ERR_VERSION;
    }
    declared_size = Rpp_ReadLe32(package_data + 12);
    if (declared_size != (uint32_t)package_size)
    {
        return PLP_ERR_BAD_LENGTH;
    }
    manifest_offset = Rpp_ReadLe32(package_data + 16);
    manifest_size = Rpp_ReadLe32(package_data + 20);
    descriptor_offset = Rpp_ReadLe32(package_data + 24);
    section_count = Rpp_ReadLe16(package_data + 28);
    descriptor_size = Rpp_ReadLe16(package_data + 30);
    payload_offset = Rpp_ReadLe32(package_data + 32);
    payload_size = Rpp_ReadLe32(package_data + 36);
    signature_offset = Rpp_ReadLe32(package_data + 200);
    signature_size = Rpp_ReadLe32(package_data + 204);

    if ((section_count == 0u) || (descriptor_size != RPP_OIP_DESCRIPTOR_SIZE))
    {
        return PLP_ERR_IMAGE_FORMAT;
    }
    if (!Rpp_RangeValid(manifest_offset, manifest_size, declared_size) ||
        !Rpp_RangeValid(descriptor_offset,
                        (uint32_t)section_count * RPP_OIP_DESCRIPTOR_SIZE,
                        declared_size) ||
        !Rpp_RangeValid(payload_offset, payload_size, declared_size))
    {
        return PLP_ERR_IMAGE_ADDRESS_OVERFLOW;
    }
    if ((manifest_offset < RPP_OIP_HEADER_SIZE) ||
        (descriptor_offset < (manifest_offset + manifest_size)) ||
        (payload_offset < (descriptor_offset + ((uint32_t)section_count * RPP_OIP_DESCRIPTOR_SIZE))))
    {
        return PLP_ERR_IMAGE_OVERLAP;
    }
    if (signature_offset == 0u)
    {
        if (signature_size != 0u)
        {
            return PLP_ERR_IMAGE_FORMAT;
        }
        canonical_size = declared_size;
    }
    else
    {
        if (!Rpp_RangeValid(signature_offset, signature_size, declared_size) ||
            (signature_offset < (payload_offset + payload_size)) ||
            ((signature_offset + signature_size) != declared_size))
        {
            return PLP_ERR_IMAGE_OVERLAP;
        }
        canonical_size = signature_offset;
    }

    memcpy(header_copy, package_data, sizeof(header_copy));
    memset(header_copy + RPP_OIP_HEADER_CRC_OFFSET, 0, 8u);
    expected_crc = Rpp_ReadLe32(package_data + RPP_OIP_HEADER_CRC_OFFSET);
    actual_crc = Rpp_Crc32IsoHdlc(header_copy, sizeof(header_copy));
    if (actual_crc != expected_crc)
    {
        return PLP_ERR_INTEGRITY_CRC;
    }
    expected_crc = Rpp_ReadLe32(package_data + RPP_OIP_PACKAGE_CRC_OFFSET);
    actual_crc = Rpp_Crc32PackageCanonical(package_data, canonical_size);
    if (actual_crc != expected_crc)
    {
        return PLP_ERR_INTEGRITY_CRC;
    }

    Rpp_Sha256(package_data + manifest_offset, manifest_size, digest);
    if (memcmp(digest, package_data + 128, 32u) != 0)
    {
        return PLP_ERR_INTEGRITY_HASH;
    }
    Rpp_Sha256(package_data + payload_offset, payload_size, digest);
    if (memcmp(digest, package_data + 160, 32u) != 0)
    {
        return PLP_ERR_INTEGRITY_HASH;
    }

    for (index = 0u; index < section_count; ++index)
    {
        descriptor = package_data + descriptor_offset + ((uint32_t)index * RPP_OIP_DESCRIPTOR_SIZE);
        section_size = Rpp_ReadLe32(descriptor + 16);
        section_offset = Rpp_ReadLe32(descriptor + 20);
        if ((section_size == 0u) || !Rpp_RangeValid(section_offset, section_size, declared_size) ||
            (section_offset < payload_offset) ||
            ((section_offset + section_size) > (payload_offset + payload_size)))
        {
            return PLP_ERR_IMAGE_ADDRESS;
        }
        if (Rpp_Crc32IsoHdlc(package_data + section_offset, section_size) != Rpp_ReadLe32(descriptor + 32))
        {
            return PLP_ERR_INTEGRITY_CRC;
        }
        Rpp_Sha256(package_data + section_offset, section_size, digest);
        if (memcmp(digest, descriptor + 36, 32u) != 0)
        {
            return PLP_ERR_INTEGRITY_HASH;
        }
    }

    info->package_size = declared_size;
    info->manifest_offset = manifest_offset;
    info->manifest_size = manifest_size;
    info->descriptor_offset = descriptor_offset;
    info->section_count = section_count;
    info->payload_offset = payload_offset;
    info->payload_size = payload_size;
    info->flags = Rpp_ReadLe32(package_data + 8);
    memcpy(info->package_uuid, package_data + 40, 16u);
    memcpy(info->image_uuid, package_data + 72, 16u);
    memcpy(info->target_profile_uuid, package_data + 88, 16u);
    return PLP_OK;
}

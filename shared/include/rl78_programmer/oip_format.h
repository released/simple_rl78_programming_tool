#ifndef RL78_PROGRAMMER_OIP_FORMAT_H
#define RL78_PROGRAMMER_OIP_FORMAT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RPP_OIP_HEADER_SIZE               256u
#define RPP_OIP_DESCRIPTOR_SIZE           96u
#define RPP_OIP_MAGIC_0                   0x4Fu
#define RPP_OIP_MAGIC_1                   0x49u
#define RPP_OIP_MAGIC_2                   0x50u
#define RPP_OIP_MAGIC_3                   0x31u
#define RPP_OIP_VERSION_MAJOR             1u
#define RPP_OIP_VERSION_MINOR             0u

#define RPP_OIP_FLAG_SIGNED               (1ul << 0)
#define RPP_OIP_FLAG_COMPRESSED           (1ul << 1)
#define RPP_OIP_FLAG_ENCRYPTED            (1ul << 2)
#define RPP_OIP_FLAG_MULTI_IMAGE          (1ul << 3)
#define RPP_OIP_FLAG_PRODUCTION_LOCKED    (1ul << 4)
#define RPP_OIP_FLAG_ALLOW_OFFLINE        (1ul << 5)
#define RPP_OIP_FLAG_ALLOW_ONLINE         (1ul << 6)
#define RPP_OIP_FLAG_ANTI_ROLLBACK        (1ul << 7)

#define RPP_OIP_SECTION_CODE_FLASH        0x0001u
#define RPP_OIP_SECTION_DATA_FLASH        0x0002u
#define RPP_OIP_SECTION_OPTION            0x0003u
#define RPP_OIP_SECTION_SECURITY          0x0004u
#define RPP_OIP_SECTION_DEVICE_CONFIG     0x0005u
#define RPP_OIP_SECTION_CALIBRATION       0x0006u
#define RPP_OIP_SECTION_SERIALIZATION     0x0007u

#define RPP_OIP_SECTION_REQUIRED          0x0001u
#define RPP_OIP_SECTION_SENSITIVE         0x0002u
#define RPP_OIP_SECTION_SPARSE            0x0004u
#define RPP_OIP_SECTION_PRESERVE_GAPS     0x0008u
#define RPP_OIP_SECTION_EXECUTABLE        0x0010u
#define RPP_OIP_SECTION_VERIFY_REQUIRED   0x0020u

#define RPP_OIP_HEADER_CRC_OFFSET         192u
#define RPP_OIP_PACKAGE_CRC_OFFSET        196u

typedef struct RppOipInfoTag
{
    uint32_t package_size;
    uint32_t manifest_offset;
    uint32_t manifest_size;
    uint32_t descriptor_offset;
    uint16_t section_count;
    uint32_t payload_offset;
    uint32_t payload_size;
    uint8_t package_uuid[16];
    uint8_t image_uuid[16];
    uint8_t target_profile_uuid[16];
    uint32_t flags;
} RppOipInfo;

uint32_t Rpp_OipValidateBasic(const uint8_t *package_data,
                              size_t package_size,
                              RppOipInfo *info);

#ifdef __cplusplus
}
#endif

#endif

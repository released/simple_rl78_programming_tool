#ifndef RL78_PROGRAMMER_INTEGRITY_H
#define RL78_PROGRAMMER_INTEGRITY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RppSha256ContextTag
{
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t block[64];
    uint32_t block_size;
} RppSha256Context;

typedef struct RppCrc32ContextTag
{
    uint32_t value;
} RppCrc32Context;

uint16_t Rpp_Crc16CcittFalse(const uint8_t *data, size_t size);
uint32_t Rpp_Crc32IsoHdlc(const uint8_t *data, size_t size);
void Rpp_Crc32Init(RppCrc32Context *context);
void Rpp_Crc32Update(RppCrc32Context *context, const uint8_t *data, size_t size);
uint32_t Rpp_Crc32Final(RppCrc32Context *context);
void Rpp_Sha256Init(RppSha256Context *context);
void Rpp_Sha256Update(RppSha256Context *context, const uint8_t *data, size_t size);
void Rpp_Sha256Final(RppSha256Context *context, uint8_t digest[32]);
void Rpp_Sha256(const uint8_t *data, size_t size, uint8_t digest[32]);

#ifdef __cplusplus
}
#endif

#endif

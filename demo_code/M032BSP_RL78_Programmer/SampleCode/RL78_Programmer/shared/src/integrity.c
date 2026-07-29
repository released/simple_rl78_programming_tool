#include "rl78_programmer/integrity.h"

#include <string.h>

static uint32_t Rpp_RotateRight(uint32_t value, uint32_t bits)
{
    return (value >> bits) | (value << (32u - bits));
}

static uint32_t Rpp_ReadBigEndian32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           (uint32_t)data[3];
}

static void Rpp_WriteBigEndian32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static void Rpp_Sha256Transform(RppSha256Context *context, const uint8_t block[64])
{
    static const uint32_t constants[64] = {
        0x428A2F98ul, 0x71374491ul, 0xB5C0FBCFul, 0xE9B5DBA5ul,
        0x3956C25Bul, 0x59F111F1ul, 0x923F82A4ul, 0xAB1C5ED5ul,
        0xD807AA98ul, 0x12835B01ul, 0x243185BEul, 0x550C7DC3ul,
        0x72BE5D74ul, 0x80DEB1FEul, 0x9BDC06A7ul, 0xC19BF174ul,
        0xE49B69C1ul, 0xEFBE4786ul, 0x0FC19DC6ul, 0x240CA1CCul,
        0x2DE92C6Ful, 0x4A7484AAul, 0x5CB0A9DCul, 0x76F988DAul,
        0x983E5152ul, 0xA831C66Dul, 0xB00327C8ul, 0xBF597FC7ul,
        0xC6E00BF3ul, 0xD5A79147ul, 0x06CA6351ul, 0x14292967ul,
        0x27B70A85ul, 0x2E1B2138ul, 0x4D2C6DFCul, 0x53380D13ul,
        0x650A7354ul, 0x766A0ABBul, 0x81C2C92Eul, 0x92722C85ul,
        0xA2BFE8A1ul, 0xA81A664Bul, 0xC24B8B70ul, 0xC76C51A3ul,
        0xD192E819ul, 0xD6990624ul, 0xF40E3585ul, 0x106AA070ul,
        0x19A4C116ul, 0x1E376C08ul, 0x2748774Cul, 0x34B0BCB5ul,
        0x391C0CB3ul, 0x4ED8AA4Aul, 0x5B9CCA4Ful, 0x682E6FF3ul,
        0x748F82EEul, 0x78A5636Ful, 0x84C87814ul, 0x8CC70208ul,
        0x90BEFFFAul, 0xA4506CEBul, 0xBEF9A3F7ul, 0xC67178F2ul
    };
    uint32_t words[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    uint32_t s0;
    uint32_t s1;
    uint32_t choose;
    uint32_t majority;
    uint32_t temp1;
    uint32_t temp2;
    uint32_t index;

    for (index = 0u; index < 16u; ++index)
    {
        words[index] = Rpp_ReadBigEndian32(block + (index * 4u));
    }
    for (index = 16u; index < 64u; ++index)
    {
        s0 = Rpp_RotateRight(words[index - 15u], 7u) ^
             Rpp_RotateRight(words[index - 15u], 18u) ^
             (words[index - 15u] >> 3u);
        s1 = Rpp_RotateRight(words[index - 2u], 17u) ^
             Rpp_RotateRight(words[index - 2u], 19u) ^
             (words[index - 2u] >> 10u);
        words[index] = words[index - 16u] + s0 + words[index - 7u] + s1;
    }

    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];
    f = context->state[5];
    g = context->state[6];
    h = context->state[7];

    for (index = 0u; index < 64u; ++index)
    {
        s1 = Rpp_RotateRight(e, 6u) ^ Rpp_RotateRight(e, 11u) ^ Rpp_RotateRight(e, 25u);
        choose = (e & f) ^ ((~e) & g);
        temp1 = h + s1 + choose + constants[index] + words[index];
        s0 = Rpp_RotateRight(a, 2u) ^ Rpp_RotateRight(a, 13u) ^ Rpp_RotateRight(a, 22u);
        majority = (a & b) ^ (a & c) ^ (b & c);
        temp2 = s0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

uint16_t Rpp_Crc16CcittFalse(const uint8_t *data, size_t size)
{
    uint16_t crc;
    size_t index;
    uint32_t bit;

    crc = 0xFFFFu;
    for (index = 0u; index < size; ++index)
    {
        crc ^= (uint16_t)((uint16_t)data[index] << 8);
        for (bit = 0u; bit < 8u; ++bit)
        {
            if ((crc & 0x8000u) != 0u)
            {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            }
            else
            {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

uint32_t Rpp_Crc32IsoHdlc(const uint8_t *data, size_t size)
{
    RppCrc32Context context;

    Rpp_Crc32Init(&context);
    Rpp_Crc32Update(&context, data, size);
    return Rpp_Crc32Final(&context);
}

void Rpp_Crc32Init(RppCrc32Context *context)
{
    if (context != 0)
    {
        context->value = 0xFFFFFFFFul;
    }
}

void Rpp_Crc32Update(RppCrc32Context *context, const uint8_t *data, size_t size)
{
    uint32_t crc;
    uint32_t bit;
    size_t index;

    if ((context == 0) || ((data == 0) && (size != 0u)))
    {
        return;
    }
    crc = context->value;
    for (index = 0u; index < size; ++index)
    {
        crc ^= (uint32_t)data[index];
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
    context->value = crc;
}

uint32_t Rpp_Crc32Final(RppCrc32Context *context)
{
    uint32_t value;
    if (context == 0)
    {
        return 0u;
    }
    value = context->value ^ 0xFFFFFFFFul;
    context->value = 0u;
    return value;
}

void Rpp_Sha256Init(RppSha256Context *context)
{
    if (context == 0)
    {
        return;
    }
    context->state[0] = 0x6A09E667ul;
    context->state[1] = 0xBB67AE85ul;
    context->state[2] = 0x3C6EF372ul;
    context->state[3] = 0xA54FF53Aul;
    context->state[4] = 0x510E527Ful;
    context->state[5] = 0x9B05688Cul;
    context->state[6] = 0x1F83D9ABul;
    context->state[7] = 0x5BE0CD19ul;
    context->bit_count = 0u;
    context->block_size = 0u;
    memset(context->block, 0, sizeof(context->block));
}

void Rpp_Sha256Update(RppSha256Context *context, const uint8_t *data, size_t size)
{
    size_t index;

    if ((context == 0) || ((data == 0) && (size != 0u)))
    {
        return;
    }
    for (index = 0u; index < size; ++index)
    {
        context->block[context->block_size] = data[index];
        ++context->block_size;
        if (context->block_size == 64u)
        {
            Rpp_Sha256Transform(context, context->block);
            context->block_size = 0u;
        }
    }
    context->bit_count += (uint64_t)size * 8u;
}

void Rpp_Sha256Final(RppSha256Context *context, uint8_t digest[32])
{
    uint32_t index;
    uint64_t bit_count;

    if ((context == 0) || (digest == 0))
    {
        return;
    }
    bit_count = context->bit_count;
    context->block[context->block_size++] = 0x80u;
    if (context->block_size > 56u)
    {
        while (context->block_size < 64u)
        {
            context->block[context->block_size++] = 0u;
        }
        Rpp_Sha256Transform(context, context->block);
        context->block_size = 0u;
    }
    while (context->block_size < 56u)
    {
        context->block[context->block_size++] = 0u;
    }
    for (index = 0u; index < 8u; ++index)
    {
        context->block[63u - index] = (uint8_t)(bit_count >> (index * 8u));
    }
    Rpp_Sha256Transform(context, context->block);
    for (index = 0u; index < 8u; ++index)
    {
        Rpp_WriteBigEndian32(digest + (index * 4u), context->state[index]);
    }
    memset(context, 0, sizeof(*context));
}

void Rpp_Sha256(const uint8_t *data, size_t size, uint8_t digest[32])
{
    RppSha256Context context;

    Rpp_Sha256Init(&context);
    Rpp_Sha256Update(&context, data, size);
    Rpp_Sha256Final(&context, digest);
}

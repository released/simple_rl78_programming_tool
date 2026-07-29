#include "rl78_programmer/plp_frame.h"

#include <string.h>

#include "rl78_programmer/integrity.h"

uint16_t Rpp_ReadLe16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

uint32_t Rpp_ReadLe32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

void Rpp_WriteLe16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

void Rpp_WriteLe32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

uint32_t Rpp_PlpEncode(uint8_t major,
                       uint8_t minor,
                       uint8_t flags,
                       uint16_t command,
                       uint16_t sequence,
                       const uint8_t *payload,
                       uint16_t payload_length,
                       uint8_t report[PLP_REPORT_SIZE])
{
    uint16_t crc;

    if (report == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    if (payload_length > PLP_PAYLOAD_SIZE)
    {
        return PLP_ERR_BAD_LENGTH;
    }
    if ((payload_length != 0u) && (payload == 0))
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    if ((flags & PLP_FLAG_RESERVED) != 0u)
    {
        return PLP_ERR_BAD_FLAGS;
    }
    if ((sequence == 0u) && ((flags & PLP_FLAG_EVENT) == 0u))
    {
        return PLP_ERR_SDK_ARGUMENT;
    }

    memset(report, 0, PLP_REPORT_SIZE);
    report[0] = PLP_MAGIC;
    report[1] = major;
    report[2] = minor;
    report[3] = flags;
    Rpp_WriteLe16(report + 4, command);
    Rpp_WriteLe16(report + 6, sequence);
    Rpp_WriteLe16(report + 8, payload_length);
    if (payload_length != 0u)
    {
        memcpy(report + PLP_HEADER_SIZE, payload, payload_length);
    }
    crc = Rpp_Crc16CcittFalse(report, PLP_CRC_OFFSET);
    Rpp_WriteLe16(report + PLP_CRC_OFFSET, crc);
    return PLP_OK;
}

uint32_t Rpp_PlpDecode(const uint8_t report[PLP_REPORT_SIZE], RppPlpFrame *frame)
{
    uint16_t expected_crc;
    uint16_t actual_crc;
    uint16_t payload_length;
    uint16_t index;
    uint8_t flags;

    if ((report == 0) || (frame == 0))
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    if (report[0] != PLP_MAGIC)
    {
        return PLP_ERR_BAD_MAGIC;
    }
    flags = report[3];
    if ((flags & PLP_FLAG_RESERVED) != 0u)
    {
        return PLP_ERR_BAD_FLAGS;
    }
    if (((flags & PLP_FLAG_ACK) != 0u) && ((flags & PLP_FLAG_NAK) != 0u))
    {
        return PLP_ERR_BAD_FLAGS;
    }
    if (((flags & (PLP_FLAG_ACK | PLP_FLAG_NAK)) != 0u) &&
        ((flags & PLP_FLAG_RESPONSE) == 0u))
    {
        return PLP_ERR_BAD_FLAGS;
    }
    payload_length = Rpp_ReadLe16(report + 8);
    if (payload_length > PLP_PAYLOAD_SIZE)
    {
        return PLP_ERR_BAD_LENGTH;
    }
    for (index = (uint16_t)(PLP_HEADER_SIZE + payload_length); index < PLP_CRC_OFFSET; ++index)
    {
        if (report[index] != 0u)
        {
            return PLP_ERR_BAD_LENGTH;
        }
    }
    expected_crc = Rpp_ReadLe16(report + PLP_CRC_OFFSET);
    actual_crc = Rpp_Crc16CcittFalse(report, PLP_CRC_OFFSET);
    if (actual_crc != expected_crc)
    {
        return PLP_ERR_CRC;
    }

    memset(frame, 0, sizeof(*frame));
    frame->major = report[1];
    frame->minor = report[2];
    frame->flags = flags;
    frame->command = Rpp_ReadLe16(report + 4);
    frame->sequence = Rpp_ReadLe16(report + 6);
    frame->payload_length = payload_length;
    if (payload_length != 0u)
    {
        memcpy(frame->payload, report + PLP_HEADER_SIZE, payload_length);
    }
    return PLP_OK;
}

uint32_t Rpp_PlpBuildResponse(uint16_t command,
                              uint16_t sequence,
                              uint8_t programmer_state,
                              uint32_t status,
                              uint8_t response_flags,
                              uint16_t detail,
                              const uint8_t *data,
                              uint16_t data_length,
                              uint8_t report[PLP_REPORT_SIZE])
{
    uint8_t payload[PLP_PAYLOAD_SIZE];
    uint8_t flags;

    if (data_length > PLP_RESPONSE_DATA_SIZE)
    {
        return PLP_ERR_BAD_LENGTH;
    }
    if ((data_length != 0u) && (data == 0))
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    memset(payload, 0, sizeof(payload));
    Rpp_WriteLe32(payload, status);
    payload[4] = programmer_state;
    payload[5] = response_flags;
    Rpp_WriteLe16(payload + 6, detail);
    if (data_length != 0u)
    {
        memcpy(payload + PLP_COMMON_RESPONSE_SIZE, data, data_length);
    }
    flags = (uint8_t)(PLP_FLAG_RESPONSE | ((status == PLP_OK) ? PLP_FLAG_ACK : PLP_FLAG_NAK));
    return Rpp_PlpEncode(PLP_VERSION_MAJOR,
                         PLP_VERSION_MINOR,
                         flags,
                         command,
                         sequence,
                         payload,
                         (uint16_t)(PLP_COMMON_RESPONSE_SIZE + data_length),
                         report);
}

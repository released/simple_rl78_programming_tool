#ifndef RL78_PROGRAMMER_PLP_FRAME_H
#define RL78_PROGRAMMER_PLP_FRAME_H

#include <stddef.h>
#include <stdint.h>

#include "plp_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RppPlpFrameTag
{
    uint8_t major;
    uint8_t minor;
    uint8_t flags;
    uint16_t command;
    uint16_t sequence;
    uint16_t payload_length;
    uint8_t payload[PLP_PAYLOAD_SIZE];
} RppPlpFrame;

uint32_t Rpp_PlpEncode(uint8_t major,
                       uint8_t minor,
                       uint8_t flags,
                       uint16_t command,
                       uint16_t sequence,
                       const uint8_t *payload,
                       uint16_t payload_length,
                       uint8_t report[PLP_REPORT_SIZE]);
uint32_t Rpp_PlpDecode(const uint8_t report[PLP_REPORT_SIZE], RppPlpFrame *frame);
uint32_t Rpp_PlpBuildResponse(uint16_t command,
                              uint16_t sequence,
                              uint8_t programmer_state,
                              uint32_t status,
                              uint8_t response_flags,
                              uint16_t detail,
                              const uint8_t *data,
                              uint16_t data_length,
                              uint8_t report[PLP_REPORT_SIZE]);
uint16_t Rpp_ReadLe16(const uint8_t *data);
uint32_t Rpp_ReadLe32(const uint8_t *data);
void Rpp_WriteLe16(uint8_t *data, uint16_t value);
void Rpp_WriteLe32(uint8_t *data, uint32_t value);

#ifdef __cplusplus
}
#endif

#endif

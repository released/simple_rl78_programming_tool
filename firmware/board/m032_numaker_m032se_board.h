#ifndef M032_NUMAKER_M032SE_BOARD_H
#define M032_NUMAKER_M032SE_BOARD_H

#include <stdint.h>

#include "m032_board_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*M032NumakerDelayUs)(uint32_t microseconds);

typedef struct M032NumakerBoardTag
{
    M032BoardIo io;
    M032NumakerDelayUs delay_us;
    uint8_t initialized;
} M032NumakerBoard;

uint32_t M032NumakerBoard_Init(M032NumakerBoard *board,
                               M032NumakerDelayUs delay_us);
uint32_t M032NumakerBoard_TryWriteByte(M032NumakerBoard *board, uint8_t value);
uint32_t M032NumakerBoard_TryReadByte(M032NumakerBoard *board, uint8_t *value);
uint8_t M032NumakerBoard_IsTransmitIdle(const M032NumakerBoard *board);

#ifdef __cplusplus
}
#endif

#endif

#ifndef M032_BOARD_IO_H
#define M032_BOARD_IO_H

#include <stdint.h>

#include "m032_board_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct M032BoardIoOpsTag
{
    void *context;
    uint32_t (*initialize)(void *context);
    uint32_t (*set_tool0_low)(void *context, uint8_t drive_low);
    uint32_t (*set_uart_mode)(void *context, uint8_t dedicated_uart);
    uint32_t (*set_reset)(void *context, uint8_t asserted);
    uint32_t (*set_target_power)(void *context, uint8_t enabled);
    uint32_t (*read_target_vdd_mv)(void *context, uint16_t *millivolts);
    uint32_t (*set_status_led)(void *context, uint8_t on);
    void (*delay_us)(void *context, uint32_t microseconds);
    uint8_t target_vdd_sense_enabled;
} M032BoardIoOps;

typedef struct M032BoardIoTag
{
    M032BoardIoOps ops;
    uint16_t last_target_vdd_mv;
    uint8_t initialized;
    uint8_t programming_mode;
} M032BoardIo;

uint32_t M032BoardIo_Init(M032BoardIo *board, const M032BoardIoOps *ops);
uint32_t M032BoardIo_SetTargetPower(M032BoardIo *board, uint8_t enabled);
uint32_t M032BoardIo_ReadTargetVdd(M032BoardIo *board, uint16_t *millivolts);
uint32_t M032BoardIo_SetStatusLed(M032BoardIo *board, uint8_t on);
uint32_t M032BoardIo_EnterProgrammingMode(M032BoardIo *board);
uint32_t M032BoardIo_LeaveSafe(M032BoardIo *board);
uint32_t M032BoardIo_RestartTarget(M032BoardIo *board);

#ifdef __cplusplus
}
#endif

#endif

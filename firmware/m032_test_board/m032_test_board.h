#ifndef M032_TEST_BOARD_H
#define M032_TEST_BOARD_H

#include <stdint.h>

#include "m032_board_io.h"
#include "m032_rl78_target_binding.h"
#include "m032_test_tool0_soft_uart.h"

#ifdef __cplusplus
extern "C" {
#endif

#define M032_TEST_LED_RED 0x01u
#define M032_TEST_LED_GREEN 0x02u

typedef void (*M032TestDelayUs)(uint32_t microseconds);

typedef struct M032TestBoardTag
{
    M032BoardIo io;
    M032TestTool0SoftUart tool0_uart;
    M032TestDelayUs delay_us;
    uint32_t button_change_ms;
    uint8_t button_raw;
    uint8_t button_stable;
    uint8_t led_mask;
    uint8_t initialized;
} M032TestBoard;

uint32_t M032TestBoard_Init(M032TestBoard *board,
                            M032TestDelayUs delay_us);
void M032TestBoard_GetRl78LinkOps(M032TestBoard *board,
                                  M032Rl78LinkOps *ops);
uint8_t M032TestBoard_PollOfflineButton(M032TestBoard *board,
                                        uint32_t now_ms);
uint8_t M032TestBoard_IsButtonPressed(void);
void M032TestBoard_SetLeds(M032TestBoard *board, uint8_t led_mask);

#ifdef __cplusplus
}
#endif

#endif

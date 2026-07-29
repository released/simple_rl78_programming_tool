#ifndef NULINK2ME_BOARD_H
#define NULINK2ME_BOARD_H

#include <stdint.h>

#include "m032_board_io.h"
#include "nulink2me_tool0_soft_uart.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NULINK2ME_LED_ICE   0x01u
#define NULINK2ME_LED_ISP   0x02u
#define NULINK2ME_LED_RED   0x04u
#define NULINK2ME_LED_GREEN 0x08u

#define NULINK2ME_BUTTON_DEBOUNCE_MS 30u

#ifndef NULINK2ME_RL78_TARGET_APPROVED
#define NULINK2ME_RL78_TARGET_APPROVED 0
#endif

typedef void (*Nulink2MeDelayUs)(uint32_t microseconds);

typedef struct Nulink2MeBoardTag
{
    M032BoardIo io;
    Nulink2MeTool0SoftUart tool0_uart;
    Nulink2MeDelayUs delay_us;
    uint32_t button_change_ms;
    uint8_t led_mask;
    uint8_t button_raw;
    uint8_t button_stable;
    uint8_t initialized;
} Nulink2MeBoard;

uint32_t Nulink2MeBoard_Init(Nulink2MeBoard *board,
                             Nulink2MeDelayUs delay_us);
uint32_t Nulink2MeBoard_ValidateProfile(void);
void Nulink2MeBoard_SetLeds(Nulink2MeBoard *board, uint8_t led_mask);
uint8_t Nulink2MeBoard_PollOfflineButton(Nulink2MeBoard *board,
                                         uint32_t now_ms);
uint32_t Nulink2MeBoard_TryWriteByte(Nulink2MeBoard *board, uint8_t value);
uint32_t Nulink2MeBoard_TryReadByte(Nulink2MeBoard *board, uint8_t *value);
uint8_t Nulink2MeBoard_IsTransmitIdle(Nulink2MeBoard *board);
uint32_t Nulink2MeBoard_PrepareAttempt(Nulink2MeBoard *board);
uint32_t Nulink2MeBoard_ParkProgrammingLink(Nulink2MeBoard *board);
uint32_t Nulink2MeBoard_ResetProgrammingLink(Nulink2MeBoard *board);
uint32_t Nulink2MeBoard_GetFailureSnapshot(
    Nulink2MeBoard *board, Nulink2MeTool0FailureSnapshot *snapshot);

#ifdef __cplusplus
}
#endif

#endif

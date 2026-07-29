#ifndef M032_STATUS_LED_H
#define M032_STATUS_LED_H

#include <stdint.h>

#include "m032_board_io.h"

#ifdef __cplusplus
extern "C" {
#endif

#define M032_LED_MODE_OFF          0u
#define M032_LED_MODE_READY        1u
#define M032_LED_MODE_TRANSFER     2u
#define M032_LED_MODE_PROGRAM      3u
#define M032_LED_MODE_PASS         4u
#define M032_LED_MODE_FAIL         5u

typedef struct M032StatusLedTag
{
    M032BoardIo *board;
    uint8_t mode;
    uint8_t last_output;
    uint8_t initialized;
} M032StatusLed;

uint32_t M032StatusLed_Init(M032StatusLed *indicator, M032BoardIo *board);
void M032StatusLed_SetMode(M032StatusLed *indicator, uint8_t mode);
uint32_t M032StatusLed_Process(M032StatusLed *indicator, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif

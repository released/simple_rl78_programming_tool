#include "m032_status_led.h"

#include <string.h>

#include "rl78_programmer/plp_contract.h"

static uint8_t OutputForMode(uint8_t mode, uint32_t now_ms)
{
    uint32_t phase;
    switch (mode)
    {
        case M032_LED_MODE_READY:
            return (now_ms % 2000u) < 100u ? 1u : 0u;
        case M032_LED_MODE_TRANSFER:
            return (now_ms % 500u) < 250u ? 1u : 0u;
        case M032_LED_MODE_PROGRAM:
            return (now_ms % 250u) < 125u ? 1u : 0u;
        case M032_LED_MODE_PASS:
            return 1u;
        case M032_LED_MODE_FAIL:
            phase = now_ms % 1000u;
            return (phase < 100u || (phase >= 200u && phase < 300u)) ? 1u : 0u;
        case M032_LED_MODE_OFF:
        default:
            return 0u;
    }
}

uint32_t M032StatusLed_Init(M032StatusLed *indicator, M032BoardIo *board)
{
    if (indicator == 0 || board == 0 || board->initialized == 0u)
    {
        return PLP_ERR_DRIVER_PROFILE;
    }
    memset(indicator, 0, sizeof(*indicator));
    indicator->board = board;
    indicator->mode = M032_LED_MODE_OFF;
    indicator->last_output = 0xFFu;
    indicator->initialized = 1u;
    return M032StatusLed_Process(indicator, 0u);
}

void M032StatusLed_SetMode(M032StatusLed *indicator, uint8_t mode)
{
    if (indicator == 0 || indicator->initialized == 0u)
    {
        return;
    }
    if (mode > M032_LED_MODE_FAIL)
    {
        mode = M032_LED_MODE_FAIL;
    }
    indicator->mode = mode;
}

uint32_t M032StatusLed_Process(M032StatusLed *indicator, uint32_t now_ms)
{
    uint8_t output;
    uint32_t status;
    if (indicator == 0 || indicator->initialized == 0u || indicator->board == 0)
    {
        return PLP_ERR_DRIVER_PROFILE;
    }
    output = OutputForMode(indicator->mode, now_ms);
    if (output == indicator->last_output)
    {
        return PLP_OK;
    }
    status = M032BoardIo_SetStatusLed(indicator->board, output);
    if (status == PLP_OK)
    {
        indicator->last_output = output;
    }
    return status;
}

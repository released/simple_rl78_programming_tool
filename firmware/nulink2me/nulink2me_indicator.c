#include "nulink2me_indicator.h"

#include <string.h>

#include "rl78_programmer/plp_contract.h"

#define INDICATOR_ANIMATION_NONE 0u
#define INDICATOR_ANIMATION_BOOT 1u
#define INDICATOR_ANIMATION_SELECTED 2u
#define INDICATOR_FLASH_HALF_MS 125u
#define INDICATOR_SLOW_HALF_MS 500u

static uint8_t FlashOn(uint32_t now_ms, uint32_t half_period_ms)
{
    return ((now_ms / half_period_ms) & 1u) == 0u ? 1u : 0u;
}

static uint8_t ProcessAnimation(Nulink2MeIndicator *indicator,
                                uint32_t now_ms)
{
    uint32_t elapsed;
    uint32_t phase;
    uint32_t phase_limit;
    uint8_t mask;
    elapsed = now_ms - indicator->animation_start_ms;
    phase = elapsed / INDICATOR_FLASH_HALF_MS;
    phase_limit = indicator->animation == INDICATOR_ANIMATION_BOOT ? 6u : 8u;
    if (phase >= phase_limit)
    {
        indicator->animation = INDICATOR_ANIMATION_NONE;
        return 0u;
    }
    mask = 0u;
    if ((phase & 1u) == 0u)
    {
        mask = NULINK2ME_LED_ICE | NULINK2ME_LED_ISP |
               NULINK2ME_LED_RED;
        if (indicator->animation == INDICATOR_ANIMATION_BOOT)
        {
            mask |= NULINK2ME_LED_GREEN;
        }
    }
    if (indicator->animation == INDICATOR_ANIMATION_SELECTED)
    {
        mask |= NULINK2ME_LED_GREEN;
    }
    Nulink2MeBoard_SetLeds(indicator->board, mask);
    return 1u;
}

uint32_t Nulink2MeIndicator_Init(Nulink2MeIndicator *indicator,
                                 Nulink2MeBoard *board,
                                 uint32_t now_ms)
{
    if (indicator == 0 || board == 0 || board->initialized == 0u)
    {
        return PLP_ERR_DRIVER_PROFILE;
    }
    memset(indicator, 0, sizeof(*indicator));
    indicator->board = board;
    indicator->animation = INDICATOR_ANIMATION_BOOT;
    indicator->animation_start_ms = now_ms;
    indicator->initialized = 1u;
    Nulink2MeBoard_SetLeds(board, NULINK2ME_LED_ICE |
                                 NULINK2ME_LED_ISP |
                                 NULINK2ME_LED_RED |
                                 NULINK2ME_LED_GREEN);
    return PLP_OK;
}

void Nulink2MeIndicator_NotifyOfflineStarted(
    Nulink2MeIndicator *indicator)
{
    if (indicator != 0 && indicator->initialized != 0u)
    {
        indicator->offline_job = 1u;
        indicator->offline_recovered = 0u;
        indicator->animation = INDICATOR_ANIMATION_NONE;
    }
}

void Nulink2MeIndicator_NotifyOfflineRecovered(
    Nulink2MeIndicator *indicator)
{
    if (indicator != 0 && indicator->initialized != 0u)
    {
        indicator->offline_job = 0u;
        indicator->offline_recovered = 1u;
        indicator->animation = INDICATOR_ANIMATION_NONE;
    }
}

void Nulink2MeIndicator_Process(Nulink2MeIndicator *indicator,
                                const ProgrammerCore *core,
                                uint32_t now_ms)
{
    uint8_t mask;
    uint8_t flash;
    if (indicator == 0 || core == 0 || indicator->initialized == 0u)
    {
        return;
    }
    if (core->session_open != 0u && indicator->session_open == 0u &&
        core->operation_state != PLP_OPERATION_RUNNING)
    {
        indicator->animation = INDICATOR_ANIMATION_SELECTED;
        indicator->animation_start_ms = now_ms;
    }
    indicator->session_open = core->session_open;
    if (ProcessAnimation(indicator, now_ms) != 0u)
    {
        return;
    }
    if (core->operation_state == PLP_OPERATION_RUNNING &&
        core->active_source_kind == 0u)
    {
        indicator->offline_job = 0u;
        indicator->offline_recovered = 0u;
    }
    if (indicator->offline_recovered != 0u)
    {
        Nulink2MeBoard_SetLeds(indicator->board, NULINK2ME_LED_ICE);
        return;
    }
    mask = NULINK2ME_LED_ICE;
    if (indicator->offline_job != 0u)
    {
        if (core->operation_state == PLP_OPERATION_RUNNING ||
            core->state == PLP_STATE_PROGRAMMING ||
            core->state == PLP_STATE_RECOVERING)
        {
            mask = NULINK2ME_LED_ISP;
            if (FlashOn(now_ms, INDICATOR_SLOW_HALF_MS) != 0u)
            {
                mask |= NULINK2ME_LED_GREEN;
            }
        }
        else if (core->operation_state == PLP_OPERATION_COMPLETED &&
                 core->terminal_result == PLP_TERMINAL_PASS)
        {
            mask = NULINK2ME_LED_ICE;
        }
        else if (core->operation_state == PLP_OPERATION_FAILED ||
                 core->operation_state == PLP_OPERATION_CANCELLED)
        {
            mask = NULINK2ME_LED_ICE;
            if (FlashOn(now_ms, INDICATOR_FLASH_HALF_MS) != 0u)
            {
                mask |= NULINK2ME_LED_ISP;
            }
        }
        Nulink2MeBoard_SetLeds(indicator->board, mask);
        return;
    }
    flash = FlashOn(now_ms, INDICATOR_FLASH_HALF_MS);
    if (core->state == PLP_STATE_RECEIVING_IMAGE ||
        core->state == PLP_STATE_VALIDATING_IMAGE)
    {
        if (FlashOn(now_ms, INDICATOR_SLOW_HALF_MS) != 0u)
        {
            mask |= NULINK2ME_LED_GREEN;
        }
    }
    else if (core->state == PLP_STATE_PROGRAMMING ||
             core->state == PLP_STATE_RECOVERING)
    {
        if (flash != 0u)
        {
            mask |= NULINK2ME_LED_GREEN;
        }
    }
    else if (core->operation_state == PLP_OPERATION_COMPLETED &&
             core->terminal_result == PLP_TERMINAL_PASS)
    {
        mask |= NULINK2ME_LED_GREEN;
    }
    else if (core->operation_state == PLP_OPERATION_FAILED ||
             core->operation_state == PLP_OPERATION_CANCELLED)
    {
        mask |= NULINK2ME_LED_GREEN;
        if (flash != 0u)
        {
            mask |= NULINK2ME_LED_RED;
        }
    }
    Nulink2MeBoard_SetLeds(indicator->board, mask);
}

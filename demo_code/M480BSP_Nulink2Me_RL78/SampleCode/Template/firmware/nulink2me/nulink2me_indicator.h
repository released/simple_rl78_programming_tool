#ifndef NULINK2ME_INDICATOR_H
#define NULINK2ME_INDICATOR_H

#include <stdint.h>

#include "nulink2me_board.h"
#include "programmer_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Nulink2MeIndicatorTag
{
    Nulink2MeBoard *board;
    uint32_t animation_start_ms;
    uint8_t animation;
    uint8_t session_open;
    uint8_t offline_job;
    uint8_t offline_recovered;
    uint8_t initialized;
} Nulink2MeIndicator;

uint32_t Nulink2MeIndicator_Init(Nulink2MeIndicator *indicator,
                                 Nulink2MeBoard *board,
                                 uint32_t now_ms);
void Nulink2MeIndicator_NotifyOfflineStarted(
    Nulink2MeIndicator *indicator);
void Nulink2MeIndicator_NotifyOfflineRecovered(
    Nulink2MeIndicator *indicator);
void Nulink2MeIndicator_Process(Nulink2MeIndicator *indicator,
                                const ProgrammerCore *core,
                                uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif

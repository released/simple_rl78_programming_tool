#ifndef NULINK2ME_TARGET_BINDING_H
#define NULINK2ME_TARGET_BINDING_H

#include "nulink2me_board.h"
#include "programmer_core.h"
#include "rl78_protocol_b.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Nulink2MeRl78TargetTag
{
    Rl78ProtocolB protocol;
    Nulink2MeBoard *board;
    uint32_t (*uptime_ms)(void *context);
    void *uptime_context;
} Nulink2MeRl78Target;

uint32_t Nulink2MeRl78Target_Install(
    Nulink2MeRl78Target *target,
    Nulink2MeBoard *board,
    uint32_t (*uptime_ms)(void *context),
    void *uptime_context,
    ProgrammerTargetOps *ops);

#ifdef __cplusplus
}
#endif

#endif

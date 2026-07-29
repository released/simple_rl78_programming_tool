#ifndef M032_RL78_TARGET_BINDING_H
#define M032_RL78_TARGET_BINDING_H

#include "m032_numaker_m032se_board.h"
#include "programmer_core.h"
#include "rl78_protocol_b.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct M032Rl78TargetTag
{
    Rl78ProtocolB protocol;
    M032BoardIo *board_io;
    void *link_context;
    uint32_t (*prepare_attempt)(void *context);
    uint32_t (*try_write_byte)(void *context, uint8_t value);
    uint32_t (*try_read_byte)(void *context, uint8_t *value);
    uint8_t (*is_transmit_idle)(void *context);
    uint32_t (*get_failure_snapshot)(
        void *context, ProgrammerTargetFailureSnapshot *snapshot);
    uint32_t (*uptime_ms)(void *context);
    void *uptime_context;
} M032Rl78Target;

typedef struct M032Rl78LinkOpsTag
{
    void *context;
    M032BoardIo *board_io;
    uint32_t (*prepare_attempt)(void *context);
    uint32_t (*try_write_byte)(void *context, uint8_t value);
    uint32_t (*try_read_byte)(void *context, uint8_t *value);
    uint8_t (*is_transmit_idle)(void *context);
    uint32_t (*get_failure_snapshot)(
        void *context, ProgrammerTargetFailureSnapshot *snapshot);
} M032Rl78LinkOps;

uint32_t M032Rl78Target_InstallLink(
    M032Rl78Target *target,
    const M032Rl78LinkOps *link,
    uint32_t (*uptime_ms)(void *context),
    void *uptime_context,
    ProgrammerTargetOps *ops);

uint32_t M032Rl78Target_Install(M032Rl78Target *target,
                                M032NumakerBoard *board,
                                uint32_t (*uptime_ms)(void *context),
                                void *uptime_context,
                                ProgrammerTargetOps *ops);

#ifdef __cplusplus
}
#endif

#endif

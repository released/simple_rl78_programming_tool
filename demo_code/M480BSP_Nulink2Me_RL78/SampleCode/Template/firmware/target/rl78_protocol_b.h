#ifndef RL78_PROTOCOL_B_H
#define RL78_PROTOCOL_B_H

#include <stdint.h>

#include "programmer_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Rl78ProtocolBTransportOpsTag
{
    void *context;
    uint32_t (*enter_programming_mode)(void *context);
    uint32_t (*prepare_attempt)(void *context);
    uint32_t (*leave_safe)(void *context);
    uint32_t (*restart_target)(void *context);
    uint32_t (*try_write_byte)(void *context, uint8_t value);
    uint32_t (*try_read_byte)(void *context, uint8_t *value);
    uint8_t (*is_transmit_idle)(void *context);
    uint32_t (*now_ms)(void *context);
    uint32_t (*get_failure_snapshot)(
        void *context, ProgrammerTargetFailureSnapshot *snapshot);
} Rl78ProtocolBTransportOps;

typedef struct Rl78ProtocolBTag
{
    Rl78ProtocolBTransportOps transport;
    ProgrammerImageOps image;
    uint32_t code_size;
    uint32_t address;
    uint32_t deadline_ms;
    uint32_t error;
    uint16_t detail;
    uint16_t attempt;
    uint16_t progress_basis_points;
    uint8_t unit[4];
    uint8_t unit_index;
    uint8_t step;
    uint8_t stage;
    uint8_t terminal;
    uint8_t terminal_result;
    uint8_t offline_retry;
    uint8_t destructive_started;
    uint8_t cancel_requested;
    ProgrammerTargetFailureSnapshot failure_snapshot;
} Rl78ProtocolB;

uint32_t Rl78ProtocolB_Init(Rl78ProtocolB *driver,
                            const Rl78ProtocolBTransportOps *transport);
void Rl78ProtocolB_GetTargetOps(Rl78ProtocolB *driver,
                                ProgrammerTargetOps *target);
uint32_t Rl78ProtocolB_GetProfileCodeSize(
    const uint8_t target_profile_uuid[16], uint32_t *code_size);

#ifdef __cplusplus
}
#endif

#endif

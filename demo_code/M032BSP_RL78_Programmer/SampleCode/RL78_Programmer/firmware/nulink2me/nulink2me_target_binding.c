#include "nulink2me_target_binding.h"

#include <string.h>

static uint32_t EnterProgrammingMode(void *context)
{
    Nulink2MeRl78Target *target;
    target = (Nulink2MeRl78Target *)context;
    return M032BoardIo_EnterProgrammingMode(&target->board->io);
}

static uint32_t PrepareAttempt(void *context)
{
    Nulink2MeRl78Target *target;
    target = (Nulink2MeRl78Target *)context;
    return Nulink2MeBoard_PrepareAttempt(target->board);
}

static uint32_t LeaveSafe(void *context)
{
    Nulink2MeRl78Target *target;
    target = (Nulink2MeRl78Target *)context;
    return M032BoardIo_LeaveSafe(&target->board->io);
}

static uint32_t RestartTarget(void *context)
{
    Nulink2MeRl78Target *target;
    target = (Nulink2MeRl78Target *)context;
    return M032BoardIo_RestartTarget(&target->board->io);
}

static uint32_t TryWriteByte(void *context, uint8_t value)
{
    Nulink2MeRl78Target *target;
    target = (Nulink2MeRl78Target *)context;
    return Nulink2MeBoard_TryWriteByte(target->board, value);
}

static uint32_t TryReadByte(void *context, uint8_t *value)
{
    Nulink2MeRl78Target *target;
    target = (Nulink2MeRl78Target *)context;
    return Nulink2MeBoard_TryReadByte(target->board, value);
}

static uint8_t IsTransmitIdle(void *context)
{
    Nulink2MeRl78Target *target;
    target = (Nulink2MeRl78Target *)context;
    return Nulink2MeBoard_IsTransmitIdle(target->board);
}

static uint32_t NowMs(void *context)
{
    Nulink2MeRl78Target *target;
    target = (Nulink2MeRl78Target *)context;
    return target->uptime_ms(target->uptime_context);
}

static uint32_t GetFailureSnapshot(
    void *context, ProgrammerTargetFailureSnapshot *snapshot)
{
    Nulink2MeRl78Target *target;
    Nulink2MeTool0FailureSnapshot link;
    uint32_t status;
    target = (Nulink2MeRl78Target *)context;
    if (snapshot == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    memset(&link, 0, sizeof(link));
    status = Nulink2MeBoard_GetFailureSnapshot(target->board, &link);
    if (status != PLP_OK)
    {
        return status;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->version = PLP_OFFLINE_FAILURE_SNAPSHOT_VERSION;
    snapshot->valid = link.valid;
    snapshot->tx_value = link.tx_value;
    snapshot->tx_bit_index = link.tx_bit_index;
    snapshot->timer_state = link.timer_state;
    snapshot->pin_flags = link.pin_flags;
    snapshot->timer_count = link.timer_count;
    return PLP_OK;
}

uint32_t Nulink2MeRl78Target_Install(
    Nulink2MeRl78Target *target,
    Nulink2MeBoard *board,
    uint32_t (*uptime_ms)(void *context),
    void *uptime_context,
    ProgrammerTargetOps *ops)
{
    Rl78ProtocolBTransportOps transport;
    uint32_t status;
    if (target == 0 || board == 0 || uptime_ms == 0 || ops == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    memset(target, 0, sizeof(*target));
    memset(ops, 0, sizeof(*ops));
    target->board = board;
    target->uptime_ms = uptime_ms;
    target->uptime_context = uptime_context;
    memset(&transport, 0, sizeof(transport));
    transport.context = target;
    transport.enter_programming_mode = EnterProgrammingMode;
    transport.prepare_attempt = PrepareAttempt;
    transport.leave_safe = LeaveSafe;
    transport.restart_target = RestartTarget;
    transport.try_write_byte = TryWriteByte;
    transport.try_read_byte = TryReadByte;
    transport.is_transmit_idle = IsTransmitIdle;
    transport.now_ms = NowMs;
    transport.get_failure_snapshot = GetFailureSnapshot;
    status = Rl78ProtocolB_Init(&target->protocol, &transport);
    if (status == PLP_OK)
    {
        Rl78ProtocolB_GetTargetOps(&target->protocol, ops);
    }
    return status;
}

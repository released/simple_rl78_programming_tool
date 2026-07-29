#include "m032_rl78_target_binding.h"

#include <string.h>

static uint32_t EnterProgrammingMode(void *context)
{
    M032Rl78Target *target;
    target = (M032Rl78Target *)context;
    return M032BoardIo_EnterProgrammingMode(target->board_io);
}

static uint32_t PrepareAttempt(void *context)
{
    M032Rl78Target *target;
    target = (M032Rl78Target *)context;
    if (target->prepare_attempt == 0)
    {
        return PLP_OK;
    }
    return target->prepare_attempt(target->link_context);
}

static uint32_t LeaveSafe(void *context)
{
    M032Rl78Target *target;
    target = (M032Rl78Target *)context;
    return M032BoardIo_LeaveSafe(target->board_io);
}

static uint32_t RestartTarget(void *context)
{
    M032Rl78Target *target;
    target = (M032Rl78Target *)context;
    return M032BoardIo_RestartTarget(target->board_io);
}

static uint32_t TryWriteByte(void *context, uint8_t value)
{
    M032Rl78Target *target;
    target = (M032Rl78Target *)context;
    return target->try_write_byte(target->link_context, value);
}

static uint32_t TryReadByte(void *context, uint8_t *value)
{
    M032Rl78Target *target;
    target = (M032Rl78Target *)context;
    return target->try_read_byte(target->link_context, value);
}

static uint8_t IsTransmitIdle(void *context)
{
    M032Rl78Target *target;
    target = (M032Rl78Target *)context;
    return target->is_transmit_idle(target->link_context);
}

static uint32_t NowMs(void *context)
{
    M032Rl78Target *target;
    target = (M032Rl78Target *)context;
    return target->uptime_ms(target->uptime_context);
}

static uint32_t GetFailureSnapshot(
    void *context, ProgrammerTargetFailureSnapshot *snapshot)
{
    M032Rl78Target *target;
    target = (M032Rl78Target *)context;
    if (target->get_failure_snapshot == 0)
    {
        if (snapshot == 0)
        {
            return PLP_ERR_SDK_ARGUMENT;
        }
        memset(snapshot, 0, sizeof(*snapshot));
        return PLP_OK;
    }
    return target->get_failure_snapshot(target->link_context, snapshot);
}

uint32_t M032Rl78Target_InstallLink(
    M032Rl78Target *target,
    const M032Rl78LinkOps *link,
    uint32_t (*uptime_ms)(void *context),
    void *uptime_context,
    ProgrammerTargetOps *ops)
{
    Rl78ProtocolBTransportOps transport;
    uint32_t status;
    if (target == 0 || link == 0 || link->board_io == 0 ||
        link->try_write_byte == 0 || link->try_read_byte == 0 ||
        link->is_transmit_idle == 0 || uptime_ms == 0 || ops == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    memset(target, 0, sizeof(*target));
    memset(ops, 0, sizeof(*ops));
    target->board_io = link->board_io;
    target->link_context = link->context;
    target->prepare_attempt = link->prepare_attempt;
    target->try_write_byte = link->try_write_byte;
    target->try_read_byte = link->try_read_byte;
    target->is_transmit_idle = link->is_transmit_idle;
    target->get_failure_snapshot = link->get_failure_snapshot;
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

static uint32_t NumakerWrite(void *context, uint8_t value)
{
    return M032NumakerBoard_TryWriteByte(
        (M032NumakerBoard *)context, value);
}

static uint32_t NumakerRead(void *context, uint8_t *value)
{
    return M032NumakerBoard_TryReadByte(
        (M032NumakerBoard *)context, value);
}

static uint8_t NumakerIdle(void *context)
{
    return M032NumakerBoard_IsTransmitIdle(
        (M032NumakerBoard *)context);
}

uint32_t M032Rl78Target_Install(M032Rl78Target *target,
                                M032NumakerBoard *board,
                                uint32_t (*uptime_ms)(void *context),
                                void *uptime_context,
                                ProgrammerTargetOps *ops)
{
    M032Rl78LinkOps link;
    if (target == 0 || board == 0 || uptime_ms == 0 || ops == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    memset(&link, 0, sizeof(link));
    link.context = board;
    link.board_io = &board->io;
    link.try_write_byte = NumakerWrite;
    link.try_read_byte = NumakerRead;
    link.is_transmit_idle = NumakerIdle;
    return M032Rl78Target_InstallLink(target, &link, uptime_ms,
                                      uptime_context, ops);
}

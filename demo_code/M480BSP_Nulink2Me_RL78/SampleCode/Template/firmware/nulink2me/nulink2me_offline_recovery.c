#include "nulink2me_offline_recovery.h"

#include <string.h>

#include "rl78_programmer/plp_contract.h"

#define RECOVERY_CONDITION_NONE     0u
#define RECOVERY_CONDITION_CONNECT  1u
#define RECOVERY_CONDITION_TERMINAL 2u
#define RECOVERY_STAGE_CONNECT      4u

static uint8_t DelayElapsed(uint32_t now_ms, uint32_t started_ms)
{
    return (uint32_t)(now_ms - started_ms) >=
        NULINK2ME_OFFLINE_RECOVERY_DELAY_MS ? 1u : 0u;
}

static void BeginCondition(Nulink2MeOfflineRecovery *recovery,
                           uint8_t condition,
                           uint32_t now_ms)
{
    recovery->condition = condition;
    recovery->condition_started_ms = now_ms;
    recovery->action_pending = 0u;
}

static void ClearCondition(Nulink2MeOfflineRecovery *recovery)
{
    recovery->condition = RECOVERY_CONDITION_NONE;
    recovery->condition_started_ms = 0u;
    recovery->action_pending = 0u;
    recovery->terminal_parked = 0u;
    recovery->terminal_latched = 0u;
}

void Nulink2MeOfflineRecovery_Init(
    Nulink2MeOfflineRecovery *recovery)
{
    if (recovery != 0)
    {
        memset(recovery, 0, sizeof(*recovery));
    }
}

void Nulink2MeOfflineRecovery_NotifyJobStarted(
    Nulink2MeOfflineRecovery *recovery)
{
    if (recovery == 0)
    {
        return;
    }
    ClearCondition(recovery);
    recovery->automatic_retry_count = 0u;
}

uint8_t Nulink2MeOfflineRecovery_Process(
    Nulink2MeOfflineRecovery *recovery,
    const ProgrammerCore *core,
    uint32_t now_ms)
{
    if (recovery == 0 || core == 0 || core->active_source_kind != 1u)
    {
        return NULINK2ME_OFFLINE_RECOVERY_NONE;
    }
    if (core->operation_state == PLP_OPERATION_RUNNING &&
        core->job_stage == RECOVERY_STAGE_CONNECT)
    {
        if (recovery->condition != RECOVERY_CONDITION_CONNECT)
        {
            BeginCondition(recovery, RECOVERY_CONDITION_CONNECT, now_ms);
            recovery->terminal_parked = 0u;
            recovery->terminal_latched = 0u;
            return NULINK2ME_OFFLINE_RECOVERY_NONE;
        }
        if (recovery->action_pending != 0u ||
            DelayElapsed(now_ms, recovery->condition_started_ms) == 0u)
        {
            return NULINK2ME_OFFLINE_RECOVERY_NONE;
        }
        recovery->action_pending =
            NULINK2ME_OFFLINE_RECOVERY_CONNECT_RESTART;
        return NULINK2ME_OFFLINE_RECOVERY_CONNECT_RESTART;
    }
    if (core->operation_state == PLP_OPERATION_RUNNING)
    {
        ClearCondition(recovery);
        return NULINK2ME_OFFLINE_RECOVERY_NONE;
    }
    if (core->operation_state != PLP_OPERATION_FAILED &&
        core->operation_state != PLP_OPERATION_CANCELLED)
    {
        ClearCondition(recovery);
        return NULINK2ME_OFFLINE_RECOVERY_NONE;
    }
    if (recovery->condition != RECOVERY_CONDITION_TERMINAL)
    {
        BeginCondition(recovery, RECOVERY_CONDITION_TERMINAL, now_ms);
        recovery->terminal_parked = 0u;
        recovery->terminal_latched = 0u;
        recovery->action_pending = NULINK2ME_OFFLINE_RECOVERY_PARK_TARGET;
        return NULINK2ME_OFFLINE_RECOVERY_PARK_TARGET;
    }
    if (recovery->action_pending != 0u)
    {
        return NULINK2ME_OFFLINE_RECOVERY_NONE;
    }
    if (recovery->terminal_parked == 0u)
    {
        if (DelayElapsed(now_ms, recovery->condition_started_ms) == 0u)
        {
            return NULINK2ME_OFFLINE_RECOVERY_NONE;
        }
        recovery->action_pending = NULINK2ME_OFFLINE_RECOVERY_PARK_TARGET;
        return NULINK2ME_OFFLINE_RECOVERY_PARK_TARGET;
    }
    if (recovery->terminal_latched != 0u ||
        DelayElapsed(now_ms, recovery->condition_started_ms) == 0u)
    {
        return NULINK2ME_OFFLINE_RECOVERY_NONE;
    }
    if (core->operation_state == PLP_OPERATION_FAILED &&
        core->terminal_result == PLP_TERMINAL_FAIL &&
        core->job_error == PLP_ERR_TARGET_DIRECTION &&
        recovery->automatic_retry_count <
            NULINK2ME_OFFLINE_COLLISION_MAX_RETRIES)
    {
        recovery->action_pending =
            NULINK2ME_OFFLINE_RECOVERY_COLLISION_RETRY;
        return NULINK2ME_OFFLINE_RECOVERY_COLLISION_RETRY;
    }
    recovery->terminal_latched = 1u;
    return NULINK2ME_OFFLINE_RECOVERY_NONE;
}

void Nulink2MeOfflineRecovery_Complete(
    Nulink2MeOfflineRecovery *recovery,
    uint8_t action,
    uint8_t succeeded,
    uint32_t now_ms)
{
    if (recovery == 0 || recovery->action_pending != action)
    {
        return;
    }
    recovery->action_pending = 0u;
    recovery->condition_started_ms = now_ms;
    if (action == NULINK2ME_OFFLINE_RECOVERY_PARK_TARGET)
    {
        recovery->terminal_parked = succeeded != 0u ? 1u : 0u;
    }
    else if (action == NULINK2ME_OFFLINE_RECOVERY_COLLISION_RETRY)
    {
        if (succeeded != 0u)
        {
            if (recovery->automatic_retry_count < 0xFFu)
            {
                recovery->automatic_retry_count++;
            }
            ClearCondition(recovery);
        }
        else
        {
            recovery->terminal_latched = 1u;
        }
    }
}

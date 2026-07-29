#ifndef NULINK2ME_OFFLINE_RECOVERY_H
#define NULINK2ME_OFFLINE_RECOVERY_H

#include <stdint.h>

#include "programmer_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NULINK2ME_OFFLINE_RECOVERY_DELAY_MS 1000u
#define NULINK2ME_OFFLINE_COLLISION_MAX_RETRIES 2u

#define NULINK2ME_OFFLINE_RECOVERY_NONE             0u
#define NULINK2ME_OFFLINE_RECOVERY_CONNECT_RESTART  1u
#define NULINK2ME_OFFLINE_RECOVERY_PARK_TARGET       2u
#define NULINK2ME_OFFLINE_RECOVERY_COLLISION_RETRY   3u

typedef struct Nulink2MeOfflineRecoveryTag
{
    uint32_t condition_started_ms;
    uint8_t condition;
    uint8_t action_pending;
    uint8_t terminal_parked;
    uint8_t terminal_latched;
    uint8_t automatic_retry_count;
} Nulink2MeOfflineRecovery;

void Nulink2MeOfflineRecovery_Init(
    Nulink2MeOfflineRecovery *recovery);
void Nulink2MeOfflineRecovery_NotifyJobStarted(
    Nulink2MeOfflineRecovery *recovery);
uint8_t Nulink2MeOfflineRecovery_Process(
    Nulink2MeOfflineRecovery *recovery,
    const ProgrammerCore *core,
    uint32_t now_ms);
void Nulink2MeOfflineRecovery_Complete(
    Nulink2MeOfflineRecovery *recovery,
    uint8_t action,
    uint8_t succeeded,
    uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif

#ifndef NULINK2ME_IAP_CONTROL_H
#define NULINK2ME_IAP_CONTROL_H

#include <stdint.h>

#include "programmer_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NULINK2ME_IAP_APPLICATION_START 0x00004000ul
#define NULINK2ME_IAP_APPLICATION_END 0x00080000ul
#define NULINK2ME_IAP_APPLICATION_CRC_ADDRESS 0x0007FFFCul
#define NULINK2ME_IAP_APPLICATION_CRC_PAGE 0x0007F000ul

typedef struct Nulink2MeIapControlTag
{
    uint32_t (*uptime_ms)(void *context);
    void *uptime_context;
    uint32_t requested_at_ms;
    uint32_t response_completed_at_ms;
    uint8_t pending;
    uint8_t response_started;
    uint8_t response_completed;
} Nulink2MeIapControl;

void Nulink2MeIapControl_Init(Nulink2MeIapControl *control,
                              uint32_t (*uptime_ms)(void *context),
                              void *uptime_context);
void Nulink2MeIapControl_GetOps(Nulink2MeIapControl *control,
                               ProgrammerBootOps *ops);
void Nulink2MeIapControl_Process(Nulink2MeIapControl *control,
                                uint32_t now_ms, uint8_t transport_busy);

#ifdef __cplusplus
}
#endif

#endif

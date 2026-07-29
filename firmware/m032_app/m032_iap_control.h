#ifndef M032_IAP_CONTROL_H
#define M032_IAP_CONTROL_H

#include <stdint.h>

#include "programmer_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define M032_IAP_APPLICATION_START 0x00004000ul
#define M032_IAP_APPLICATION_END 0x0000FC00ul
#define M032_IAP_APPLICATION_CRC_ADDRESS 0x0000FBFCul
#define M032_IAP_APPLICATION_CRC_PAGE 0x0000FA00ul

typedef struct M032IapControlTag
{
    uint32_t (*uptime_ms)(void *context);
    void *uptime_context;
    uint32_t requested_at_ms;
    uint32_t response_completed_at_ms;
    uint8_t pending;
    uint8_t response_started;
    uint8_t response_completed;
} M032IapControl;

void M032IapControl_Init(M032IapControl *control,
                         uint32_t (*uptime_ms)(void *context),
                         void *uptime_context);
void M032IapControl_GetOps(M032IapControl *control, ProgrammerBootOps *ops);
void M032IapControl_Process(M032IapControl *control, uint32_t now_ms,
                           uint8_t transport_busy);

#ifdef __cplusplus
}
#endif

#endif

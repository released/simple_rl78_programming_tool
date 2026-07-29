#ifndef M032_NUMICRO_HID_PORT_H
#define M032_NUMICRO_HID_PORT_H

#include <stdint.h>

#include "NuMicro.h"
#include "m032_application.h"

#ifdef __cplusplus
extern "C" {
#endif

#define M032_HID_EP0_MAX_PACKET 8u
#define M032_HID_REPORT_SIZE 64u
#define M032_HID_EP2_BUFFER_BASE 16u
#define M032_HID_EP3_BUFFER_BASE 80u
#define M032_HID_INT_IN_NUMBER 1u
#define M032_HID_INT_OUT_NUMBER 2u

typedef struct M032NumicroHidPortTag
{
    M032Application *application;
    volatile uint8_t in_busy;
} M032NumicroHidPort;

extern const S_USBD_INFO_T g_M032HidDeviceInfo;

uint32_t M032NumicroHidPort_Init(M032NumicroHidPort *port,
                                 M032Application *application);
void M032NumicroHidPort_GetOps(M032NumicroHidPort *port, M032UsbPortOps *ops);

#ifdef __cplusplus
}
#endif

#endif

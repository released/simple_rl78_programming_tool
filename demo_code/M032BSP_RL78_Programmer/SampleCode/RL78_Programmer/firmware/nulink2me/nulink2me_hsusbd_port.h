#ifndef NULINK2ME_HSUSBD_PORT_H
#define NULINK2ME_HSUSBD_PORT_H

#include <stdint.h>

#include "NuMicro.h"
#include "nulink2me_application.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NULINK2ME_HID_REPORT_SIZE 64u
#define NULINK2ME_HID_CEP_BUFFER_BASE 0u
#define NULINK2ME_HID_CEP_BUFFER_SIZE 64u
#define NULINK2ME_HID_IN_BUFFER_BASE 0x200u
#define NULINK2ME_HID_OUT_BUFFER_BASE 0x300u
#define NULINK2ME_HID_ENDPOINT_BUFFER_SIZE 64u
#define NULINK2ME_HID_INT_IN_NUMBER 1u
#define NULINK2ME_HID_INT_OUT_NUMBER 2u

typedef struct Nulink2MeHsusbdPortTag
{
    Nulink2MeApplication *application;
    uint8_t in_report[NULINK2ME_HID_REPORT_SIZE];
    volatile uint8_t in_busy;
} Nulink2MeHsusbdPort;

extern S_HSUSBD_INFO_T g_Nulink2MeHidDeviceInfo;

uint32_t Nulink2MeHsusbdPort_Init(Nulink2MeHsusbdPort *port,
                                  Nulink2MeApplication *application);
void Nulink2MeHsusbdPort_GetOps(Nulink2MeHsusbdPort *port,
                                Nulink2MeUsbPortOps *ops);

#ifdef __cplusplus
}
#endif

#endif

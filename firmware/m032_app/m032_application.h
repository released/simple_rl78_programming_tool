#ifndef M032_APPLICATION_H
#define M032_APPLICATION_H

#include <stdint.h>

#include "m032_board_io.h"
#include "m032_package_store.h"
#include "m032_programmer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct M032LinkPortOpsTag
{
    void *context;
    uint8_t (*can_send)(void *context);
    uint32_t (*send)(void *context, const uint8_t report[PLP_REPORT_SIZE]);
} M032LinkPortOps;

typedef M032LinkPortOps M032UsbPortOps;

typedef struct M032ApplicationConfigTag
{
    const M032StorageBackend *storage_backend;
    const ProgrammerTargetOps *target;
    M032BoardIo *board_io;
    const M032ProgrammerIdentity *identity;
    M032LinkPortOps link;
    /* Kept for source compatibility with the existing USB HID application. */
    M032UsbPortOps usb;
    ProgrammerBootOps boot;
    uint32_t capabilities;
    uint32_t autorun_grace_ms;
    uint8_t offline_reset_triggered;
    uint32_t (*uptime_ms)(void *context);
    void *uptime_context;
} M032ApplicationConfig;

typedef struct M032ApplicationTag
{
    M032Programmer programmer;
    M032PackageStore package_store;
    M032LinkPortOps link;
    uint32_t autorun_grace_ms;
    uint8_t storage_ready;
} M032Application;

uint32_t M032Application_Init(M032Application *application,
                              const M032ApplicationConfig *config);
uint32_t M032Application_OnUsbOut(M032Application *application,
                                  const uint8_t *report, uint32_t length);
uint32_t M032Application_OnLinkReport(M032Application *application,
                                     const uint8_t *report, uint32_t length);
void M032Application_Process(M032Application *application, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif

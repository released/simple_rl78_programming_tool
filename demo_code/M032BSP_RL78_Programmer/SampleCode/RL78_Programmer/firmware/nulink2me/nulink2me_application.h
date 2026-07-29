#ifndef NULINK2ME_APPLICATION_H
#define NULINK2ME_APPLICATION_H

#include <stdint.h>

#include "m032_package_store.h"
#include "m032_programmer.h"
#include "nulink2me_board.h"
#include "nulink2me_image_cache.h"
#include "nulink2me_indicator.h"
#include "nulink2me_offline_recovery.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Nulink2MeUsbPortOpsTag
{
    void *context;
    uint8_t (*can_send)(void *context);
    uint32_t (*send)(void *context,
                     const uint8_t report[PLP_REPORT_SIZE]);
} Nulink2MeUsbPortOps;

typedef struct Nulink2MeApplicationConfigTag
{
    const M032StorageBackend *storage_backend;
    const ProgrammerTargetOps *target;
    Nulink2MeBoard *board;
    Nulink2MeUsbPortOps usb;
    ProgrammerBootOps boot;
    uint32_t capabilities;
    uint32_t (*uptime_ms)(void *context);
    void *uptime_context;
    uint8_t *image_cache_buffer;
    uint32_t image_cache_capacity;
} Nulink2MeApplicationConfig;

typedef struct Nulink2MeApplicationTag
{
    M032Programmer programmer;
    M032PackageStore package_store;
    Nulink2MeImageCache image_cache;
    Nulink2MeIndicator indicator;
    Nulink2MeOfflineRecovery offline_recovery;
    Nulink2MeBoard *board;
    Nulink2MeUsbPortOps usb;
    uint8_t storage_ready;
} Nulink2MeApplication;

uint32_t Nulink2MeApplication_Init(
    Nulink2MeApplication *application,
    const Nulink2MeApplicationConfig *config);
uint32_t Nulink2MeApplication_OnUsbOut(
    Nulink2MeApplication *application,
    const uint8_t *report,
    uint32_t length);
void Nulink2MeApplication_OnUsbDisconnected(
    Nulink2MeApplication *application);
void Nulink2MeApplication_Process(Nulink2MeApplication *application,
                                  uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif

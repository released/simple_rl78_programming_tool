#include "m032_application.h"

#include <string.h>

uint32_t M032Application_Init(M032Application *application,
                              const M032ApplicationConfig *config)
{
    ProgrammerStorageOps storage;
    ProgrammerModeOps mode;
    M032LinkPortOps link;
    uint32_t capabilities;
    uint32_t status;
    if (application == 0 || config == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    memset(&link, 0, sizeof(link));
    if (config->link.can_send != 0 && config->link.send != 0)
    {
        memcpy(&link, &config->link, sizeof(link));
    }
    else if (config->usb.can_send != 0 && config->usb.send != 0)
    {
        memcpy(&link, &config->usb, sizeof(link));
    }
    else
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    memset(application, 0, sizeof(*application));
    memcpy(&application->link, &link, sizeof(link));
    application->autorun_grace_ms = config->autorun_grace_ms;
    if (config->identity != 0)
    {
        M032Programmer_InitWithIdentity(&application->programmer,
                                        config->identity);
    }
    else
    {
        M032Programmer_Init(&application->programmer);
    }
    capabilities = config->capabilities;
    if (config->target == 0 || config->target->profile_ready == 0u ||
        config->target->start == 0)
    {
        capabilities &= ~(PLP_CAP_TARGET_PROGRAM | PLP_CAP_TARGET_INFORMATION |
                          PLP_CAP_TARGET_READBACK | PLP_CAP_TARGET_RECOVERY);
    }
    memset(&storage, 0, sizeof(storage));
    memset(&mode, 0, sizeof(mode));
    if (config->storage_backend != 0)
    {
        status = M032PackageStore_Init(&application->package_store,
                                       config->storage_backend);
        if (status != PLP_OK)
        {
            return status;
        }
        M032PackageStore_GetStorageOps(&application->package_store, &storage);
        M032PackageStore_GetModeOps(&application->package_store, &mode);
        application->storage_ready = 1u;
        capabilities &= ~PLP_CAP_STORAGE_AB;
        capabilities |= PLP_CAP_OFFLINE_PACKAGE | PLP_CAP_ATOMIC_ACTIVATION |
                        PLP_CAP_CONFIG_SNAPSHOT |
                        PLP_CAP_OFFLINE_BUTTON |
                        PLP_CAP_OFFLINE_AUTORUN;
    }
    M032Programmer_Configure(&application->programmer,
                             application->storage_ready != 0u ? &storage : 0,
                             config->target, capabilities,
                             config->uptime_ms, config->uptime_context);
    if (config->boot.prepare != 0 || config->boot.prepare_reset != 0)
    {
        memcpy(&application->programmer.core.config.boot, &config->boot,
               sizeof(config->boot));
    }
    if (config->boot.prepare != 0)
    {
        application->programmer.core.config.capabilities |=
            PLP_CAP_PROGRAMMER_UPDATE;
    }
    else
    {
        application->programmer.core.config.capabilities &=
            ~PLP_CAP_PROGRAMMER_UPDATE;
    }
    if (config->boot.prepare_reset != 0)
    {
        application->programmer.core.config.capabilities |=
            PLP_CAP_PROGRAMMER_RESET;
    }
    else
    {
        application->programmer.core.config.capabilities &=
            ~PLP_CAP_PROGRAMMER_RESET;
    }
    if (application->storage_ready != 0u)
    {
        status = M032Programmer_ConfigureMode(
            &application->programmer, &mode, config->offline_reset_triggered);
        if (status != PLP_OK)
        {
            return status;
        }
    }
    if (config->board_io != 0)
    {
        status = M032Programmer_AttachBoardIo(&application->programmer,
                                              config->board_io);
        if (status != PLP_OK)
        {
            return status;
        }
    }
    return PLP_OK;
}

uint32_t M032Application_OnUsbOut(M032Application *application,
                                  const uint8_t *report, uint32_t length)
{
    return M032Application_OnLinkReport(application, report, length);
}

uint32_t M032Application_OnLinkReport(M032Application *application,
                                     const uint8_t *report, uint32_t length)
{
    if (application == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    return M032Programmer_OnOutReport(&application->programmer, report, length);
}

void M032Application_Process(M032Application *application, uint32_t now_ms)
{
    uint8_t report[PLP_REPORT_SIZE];
    uint32_t status;
    if (application == 0)
    {
        return;
    }
    M032Programmer_Process(&application->programmer);
    (void)M032Programmer_ProcessOfflineAutorun(&application->programmer,
                                               now_ms,
                                               application->autorun_grace_ms);
    if (application->programmer.status_led_attached != 0u)
    {
        (void)M032Programmer_UpdateIndicator(&application->programmer, now_ms);
    }
    if (application->link.can_send(application->link.context) == 0u)
    {
        return;
    }
    if (M032Programmer_TakeInReport(&application->programmer, report) == 0u)
    {
        return;
    }
    status = application->link.send(application->link.context, report);
    if (status != PLP_OK)
    {
        memcpy(application->programmer.in_report, report, PLP_REPORT_SIZE);
        application->programmer.in_pending = 1u;
    }
}

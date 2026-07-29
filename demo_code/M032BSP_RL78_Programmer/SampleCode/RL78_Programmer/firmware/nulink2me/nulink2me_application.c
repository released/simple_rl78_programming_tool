#include "nulink2me_application.h"

#include <string.h>

#include "rl78_protocol_b.h"

static const M032ProgrammerIdentity g_Nulink2MeIdentity =
{
    "Nu-Link2-Me RL78 Programmer",
    {
        0x4Eu, 0x4Cu, 0x32u, 0x4Du, 0x52u, 0x4Cu, 0x37u, 0x38u,
        0x80u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u
    },
    {
        0x4Eu, 0x4Cu, 0x32u, 0x4Du, 0x4Du, 0x34u, 0x38u, 0x34u,
        0x80u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u
    },
    {1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
    {1u, 0u, 1u, 0u, 5u, 0u, 0u, 0u}
};

uint32_t Nulink2MeApplication_Init(
    Nulink2MeApplication *application,
    const Nulink2MeApplicationConfig *config)
{
    ProgrammerStorageOps storage;
    ProgrammerModeOps mode;
    uint32_t capabilities;
    uint32_t status;
    if (application == 0 || config == 0 || config->board == 0 ||
        config->usb.can_send == 0 || config->usb.send == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    memset(application, 0, sizeof(*application));
    application->board = config->board;
    memcpy(&application->usb, &config->usb, sizeof(config->usb));
    M032Programmer_InitWithIdentity(&application->programmer,
                                    &g_Nulink2MeIdentity);
    capabilities = config->capabilities;
    if (config->target == 0 || config->target->profile_ready == 0u ||
        config->target->start == 0)
    {
        capabilities &= ~(PLP_CAP_TARGET_PROGRAM |
                          PLP_CAP_TARGET_INFORMATION |
                          PLP_CAP_TARGET_READBACK |
                          PLP_CAP_TARGET_RECOVERY);
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
        status = Nulink2MeImageCache_Init(
            &application->image_cache, &storage,
            config->image_cache_buffer, config->image_cache_capacity,
            Rl78ProtocolB_GetProfileCodeSize);
        if (status != PLP_OK)
        {
            return status;
        }
        Nulink2MeImageCache_GetStorageOps(&application->image_cache, &storage);
        M032PackageStore_GetModeOps(&application->package_store, &mode);
        application->storage_ready = 1u;
        capabilities &= ~PLP_CAP_STORAGE_AB;
        capabilities |= PLP_CAP_OFFLINE_PACKAGE |
                        PLP_CAP_ATOMIC_ACTIVATION |
                        PLP_CAP_CONFIG_SNAPSHOT |
                        PLP_CAP_OFFLINE_BUTTON |
                        PLP_CAP_OFFLINE_AUTORUN;
    }
    M032Programmer_Configure(&application->programmer,
                             application->storage_ready != 0u ? &storage : 0,
                             config->target,
                             capabilities,
                             config->uptime_ms,
                             config->uptime_context);
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
        status = M032Programmer_ConfigureMode(&application->programmer,
                                              &mode, 0u);
        if (status != PLP_OK)
        {
            return status;
        }
        if (application->programmer.core.operating_mode ==
                PLP_OPERATING_MODE_OFFLINE_ARMED)
        {
            (void)Nulink2MeImageCache_PreloadActive(
                &application->image_cache);
        }
    }
    status = Nulink2MeIndicator_Init(&application->indicator,
                                     application->board, 0u);
    if (status == PLP_OK)
    {
        Nulink2MeOfflineRecovery_Init(&application->offline_recovery);
    }
    return status;
}

uint32_t Nulink2MeApplication_OnUsbOut(
    Nulink2MeApplication *application,
    const uint8_t *report,
    uint32_t length)
{
    if (application == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    return M032Programmer_OnOutReport(&application->programmer,
                                      report, length);
}

void Nulink2MeApplication_OnUsbDisconnected(
    Nulink2MeApplication *application)
{
    if (application == 0)
    {
        return;
    }
    M032Programmer_OnTransportDisconnected(&application->programmer);
}

void Nulink2MeApplication_Process(Nulink2MeApplication *application,
                                  uint32_t now_ms)
{
    uint8_t report[PLP_REPORT_SIZE];
    uint8_t recovery_action;
    uint32_t status;
    if (application == 0)
    {
        return;
    }
    if (application->storage_ready != 0u &&
        Nulink2MeBoard_PollOfflineButton(application->board, now_ms) != 0u &&
        application->programmer.core.operating_mode ==
            PLP_OPERATING_MODE_OFFLINE_ARMED)
    {
        status = M032Programmer_OnOfflineButton(&application->programmer);
        if (status == PLP_OK)
        {
            Nulink2MeOfflineRecovery_NotifyJobStarted(
                &application->offline_recovery);
            Nulink2MeIndicator_NotifyOfflineStarted(&application->indicator);
        }
    }
    M032Programmer_Process(&application->programmer);
    recovery_action = Nulink2MeOfflineRecovery_Process(
        &application->offline_recovery,
        &application->programmer.core, now_ms);
    if (recovery_action != NULINK2ME_OFFLINE_RECOVERY_NONE)
    {
        status = PLP_OK;
        if (recovery_action ==
                NULINK2ME_OFFLINE_RECOVERY_CONNECT_RESTART)
        {
            status = Nulink2MeBoard_ResetProgrammingLink(
                application->board);
            if (status == PLP_OK)
            {
                status = M032Programmer_OnOfflineButton(
                    &application->programmer);
            }
        }
        else if (recovery_action ==
                     NULINK2ME_OFFLINE_RECOVERY_PARK_TARGET)
        {
            status = Nulink2MeBoard_ParkProgrammingLink(
                application->board);
        }
        else if (recovery_action ==
                     NULINK2ME_OFFLINE_RECOVERY_COLLISION_RETRY)
        {
            status = M032Programmer_OnOfflineButton(
                &application->programmer);
        }
        Nulink2MeOfflineRecovery_Complete(
            &application->offline_recovery, recovery_action,
            status == PLP_OK ? 1u : 0u, now_ms);
        if (status == PLP_OK &&
            (recovery_action ==
                 NULINK2ME_OFFLINE_RECOVERY_CONNECT_RESTART ||
             recovery_action ==
                 NULINK2ME_OFFLINE_RECOVERY_COLLISION_RETRY))
        {
            Nulink2MeIndicator_NotifyOfflineStarted(
                &application->indicator);
        }
    }
    Nulink2MeIndicator_Process(&application->indicator,
                               &application->programmer.core, now_ms);
    if (application->usb.can_send(application->usb.context) == 0u)
    {
        return;
    }
    if (M032Programmer_TakeInReport(&application->programmer, report) == 0u)
    {
        return;
    }
    status = application->usb.send(application->usb.context, report);
    if (status != PLP_OK)
    {
        memcpy(application->programmer.in_report, report, PLP_REPORT_SIZE);
        application->programmer.in_pending = 1u;
    }
}

#include "nulink2me_iap_control.h"

#include <string.h>

#include "NuMicro.h"

#define NULINK2ME_IAP_ACK_DELAY_MS 20u
#define NULINK2ME_IAP_RESET_FALLBACK_MS 500u
#define NULINK2ME_IAP_CBS_APROM_WITH_IAP (2UL << FMC_ISPSTS_CBS_Pos)

static uint8_t IsIapConfigurationValid(void)
{
    if ((FMC->ISPSTS & FMC_ISPSTS_CBS_Msk) !=
        NULINK2ME_IAP_CBS_APROM_WITH_IAP)
    {
        return 0u;
    }
    return ((FMC->ISPSTS & FMC_ISPSTS_MBS_Msk) != 0UL) ? 1u : 0u;
}

static int32_t SetVectorPageAddress(uint32_t page_address)
{
    if (IsIapConfigurationValid() == 0u)
    {
        return -1;
    }
    FMC_CLR_FAIL_FLAG();
    if (FMC_SetVectorPageAddr(page_address) != 0)
    {
        return -1;
    }
    if (FMC_GET_FAIL_FLAG() != 0UL)
    {
        FMC_CLR_FAIL_FLAG();
        return -1;
    }
    if (FMC_GetVECMAP() != (page_address & FMC_ISPSTS_VECMAP_Msk))
    {
        return -1;
    }
    return 0;
}

static void ResetThroughBootloader(void)
{
    uint32_t saved_primask;

    SYS_UnlockReg();
    FMC_Open();
    saved_primask = __get_PRIMASK();
    __set_PRIMASK(1u);
    if (SetVectorPageAddress(0u) != 0)
    {
        __set_PRIMASK(saved_primask);
        return;
    }
    __DSB();
    __ISB();
    /* Reset M484 I/O and peripherals before bootloader validation. */
    SYS_ResetChip();
    for (;;)
    {
    }
}

static void ScheduleReset(Nulink2MeIapControl *control)
{
    control->requested_at_ms = control->uptime_ms(control->uptime_context);
    control->response_completed_at_ms = 0u;
    control->response_started = 0u;
    control->response_completed = 0u;
    control->pending = 1u;
}

static uint32_t PrepareBootloader(void *context)
{
    Nulink2MeIapControl *control;
    int32_t erase_status;
    control = (Nulink2MeIapControl *)context;
    if (control == 0 || control->uptime_ms == 0)
    {
        return PLP_ERR_UPDATE_APPLY;
    }
    if (control->pending != 0u)
    {
        return PLP_ERR_PROGRAM_BUSY;
    }
    SYS_UnlockReg();
    FMC_Open();
    if (IsIapConfigurationValid() == 0u)
    {
        return PLP_ERR_UPDATE_INCOMPATIBLE;
    }
    FMC_ENABLE_AP_UPDATE();
    erase_status = FMC_Erase(NULINK2ME_IAP_APPLICATION_CRC_PAGE);
    FMC_DISABLE_AP_UPDATE();
    if (erase_status != 0)
    {
        return PLP_ERR_UPDATE_APPLY;
    }
    ScheduleReset(control);
    return PLP_OK;
}

static uint32_t PrepareReset(void *context)
{
    Nulink2MeIapControl *control;
    control = (Nulink2MeIapControl *)context;
    if (control == 0 || control->uptime_ms == 0)
    {
        return PLP_ERR_UPDATE_APPLY;
    }
    if (control->pending != 0u)
    {
        return PLP_ERR_PROGRAM_BUSY;
    }
    SYS_UnlockReg();
    FMC_Open();
    if (IsIapConfigurationValid() == 0u)
    {
        return PLP_ERR_UPDATE_INCOMPATIBLE;
    }
    ScheduleReset(control);
    return PLP_OK;
}

void Nulink2MeIapControl_Init(Nulink2MeIapControl *control,
                              uint32_t (*uptime_ms)(void *context),
                              void *uptime_context)
{
    if (control == 0)
    {
        return;
    }
    memset(control, 0, sizeof(*control));
    control->uptime_ms = uptime_ms;
    control->uptime_context = uptime_context;
}

void Nulink2MeIapControl_GetOps(Nulink2MeIapControl *control,
                               ProgrammerBootOps *ops)
{
    if (control == 0 || ops == 0)
    {
        return;
    }
    memset(ops, 0, sizeof(*ops));
    ops->context = control;
    ops->prepare = PrepareBootloader;
    ops->prepare_reset = PrepareReset;
}

void Nulink2MeIapControl_Process(Nulink2MeIapControl *control,
                                uint32_t now_ms, uint8_t transport_busy)
{
    if (control == 0 || control->pending == 0u)
    {
        return;
    }
    if (transport_busy != 0u)
    {
        control->response_started = 1u;
        control->response_completed = 0u;
    }
    else if (control->response_started != 0u &&
             control->response_completed == 0u)
    {
        control->response_completed = 1u;
        control->response_completed_at_ms = now_ms;
    }
    if ((control->response_completed != 0u &&
         (uint32_t)(now_ms - control->response_completed_at_ms) >=
             NULINK2ME_IAP_ACK_DELAY_MS) ||
        (uint32_t)(now_ms - control->requested_at_ms) >=
            NULINK2ME_IAP_RESET_FALLBACK_MS)
    {
        ResetThroughBootloader();
    }
}

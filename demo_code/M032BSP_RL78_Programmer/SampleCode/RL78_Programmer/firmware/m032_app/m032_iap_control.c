#include "m032_iap_control.h"

#include <string.h>

#include "NuMicro.h"

#define M032_IAP_ACK_DELAY_MS 20u
#define M032_IAP_RESET_FALLBACK_MS 500u
#define M032_IAP_CBS_APROM_WITH_IAP (2UL << FMC_ISPSTS_CBS_Pos)

static uint8_t IsIapConfigurationValid(void)
{
    return ((FMC->ISPSTS & FMC_ISPSTS_CBS_Msk) ==
            M032_IAP_CBS_APROM_WITH_IAP) ? 1u : 0u;
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
    SYS->IPRST0 = SYS_IPRST0_CPURST_Msk;
    for (;;)
    {
    }
}

static void ScheduleReset(M032IapControl *control)
{
    control->requested_at_ms = control->uptime_ms(control->uptime_context);
    control->response_completed_at_ms = 0u;
    control->response_started = 0u;
    control->response_completed = 0u;
    control->pending = 1u;
}

static uint32_t PrepareBootloader(void *context)
{
    M032IapControl *control;
    int32_t erase_status;
    control = (M032IapControl *)context;
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
    erase_status = FMC_Erase(M032_IAP_APPLICATION_CRC_PAGE);
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
    M032IapControl *control;
    control = (M032IapControl *)context;
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

void M032IapControl_Init(M032IapControl *control,
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

void M032IapControl_GetOps(M032IapControl *control, ProgrammerBootOps *ops)
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

void M032IapControl_Process(M032IapControl *control, uint32_t now_ms,
                           uint8_t transport_busy)
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
             M032_IAP_ACK_DELAY_MS) ||
        (uint32_t)(now_ms - control->requested_at_ms) >=
            M032_IAP_RESET_FALLBACK_MS)
    {
        ResetThroughBootloader();
    }
}

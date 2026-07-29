#include <string.h>

#include "NuMicro.h"
#include "m032_application.h"
#include "m032_iap_control.h"
#include "m032_numicro_fmc_storage.h"
#include "m032_numicro_hid_port.h"
#include "m032_rl78_target_binding.h"

#define AUTORUN_GRACE_MS 2000u
#define M032_APROM_BYTES (128u * 1024u)
#define HIRC_TRIM_INITIAL_REGISTER (SYS_BASE + 0x118u)

static volatile uint32_t g_milliseconds;
static uint32_t g_hirc_trim_initial;

static uint8_t CaptureExternalResetTrigger(void)
{
    uint32_t reset_status;
    uint8_t triggered;
    reset_status = SYS->RSTSTS;
    triggered = ((reset_status & SYS_RSTSTS_PINRF_Msk) != 0u &&
                 (reset_status & SYS_RSTSTS_PORF_Msk) == 0u) ? 1u : 0u;
    SYS->RSTSTS = reset_status;
    return triggered;
}

void SysTick_Handler(void)
{
    g_milliseconds++;
}

static uint32_t UptimeMs(void *context)
{
    (void)context;
    return g_milliseconds;
}

static void DelayUs(uint32_t microseconds)
{
    CLK_SysTickDelay(microseconds);
}

static void SystemInitialize(void)
{
    SYS_UnlockReg();
    CLK_EnableXtalRC(CLK_PWRCTL_HIRCEN_Msk);
    CLK_WaitClockReady(CLK_STATUS_HIRCSTB_Msk);
    CLK_SetHCLK(CLK_CLKSEL0_HCLKSEL_HIRC, CLK_CLKDIV0_HCLK(1));
    CLK_SetModuleClock(USBD_MODULE, CLK_CLKSEL0_USBDSEL_HIRC,
                       CLK_CLKDIV0_USB(1));
    CLK_EnableModuleClock(USBD_MODULE);
    SystemCoreClockUpdate();
    SYS_LockReg();
    (void)SysTick_Config(SystemCoreClock / 1000u);
}

static void UsbTrimProcess(void)
{
    if ((SYS->HIRCTRIMCTL & SYS_HIRCTRIMCTL_FREQSEL_Msk) != 1u &&
        (USBD->INTSTS & USBD_INTSTS_SOFIF_Msk) != 0u)
    {
        USBD->INTSTS = USBD_INTSTS_SOFIF_Msk;
        SYS->HIRCTRIMCTL = 0x01u;
        SYS->HIRCTRIMCTL |= SYS_HIRCTRIMCTL_REFCKSEL_Msk;
    }
    if ((SYS->HIRCTRIMSTS &
         (SYS_HIRCTRIMSTS_CLKERIF_Msk | SYS_HIRCTRIMSTS_TFAILIF_Msk)) != 0u)
    {
        M32(HIRC_TRIM_INITIAL_REGISTER) = g_hirc_trim_initial;
        SYS->HIRCTRIMCTL = 0u;
        SYS->HIRCTRIMSTS = SYS_HIRCTRIMSTS_CLKERIF_Msk |
                           SYS_HIRCTRIMSTS_TFAILIF_Msk;
        USBD->INTSTS = USBD_INTSTS_SOFIF_Msk;
    }
}

int main(void)
{
    static M032Application application;
    M032ApplicationConfig config;
    M032NumakerBoard board;
    M032NumicroHidPort hid;
    M032NumicroFmcStorage fmc_storage;
    M032IapControl iap_control;
    M032UsbPortOps usb;
    ProgrammerTargetOps target;
    ProgrammerBootOps boot;
    M032Rl78Target rl78_target;
    const M032StorageBackend *storage_backend;
    const ProgrammerTargetOps *target_ops;
    uint32_t status;
    uint8_t offline_reset_triggered;
    offline_reset_triggered = CaptureExternalResetTrigger();
    SystemInitialize();
    status = M032NumakerBoard_Init(&board, DelayUs);
    if (status != PLP_OK)
    {
        for (;;)
        {
        }
    }
    memset(&application, 0, sizeof(application));
    memset(&hid, 0, sizeof(hid));
    M032NumicroHidPort_GetOps(&hid, &usb);
    storage_backend = 0;
#if defined(M032_INTERNAL_STORAGE_BASE) && defined(M032_INTERNAL_STORAGE_SIZE) && \
    defined(M032_FIRMWARE_END_ADDRESS)
    status = M032NumicroFmcStorage_Init(&fmc_storage,
                                        M032_INTERNAL_STORAGE_BASE,
                                        M032_INTERNAL_STORAGE_SIZE,
                                        M032_FIRMWARE_END_ADDRESS,
                                        M032_APROM_BYTES);
    if (status == PLP_OK)
    {
        storage_backend = &fmc_storage.backend;
    }
#else
    (void)fmc_storage;
#endif
    target_ops = 0;
    status = M032Rl78Target_Install(&rl78_target, &board, UptimeMs, 0, &target);
    if (status == PLP_OK && target.profile_ready != 0u)
    {
        target_ops = &target;
    }
    memset(&config, 0, sizeof(config));
    memset(&boot, 0, sizeof(boot));
    M032IapControl_Init(&iap_control, UptimeMs, 0);
    M032IapControl_GetOps(&iap_control, &boot);
    config.storage_backend = storage_backend;
    config.target = target_ops;
    config.board_io = &board.io;
    config.capabilities = PLP_CAP_TARGET_PROGRAM;
    memcpy(&config.boot, &boot, sizeof(boot));
    memcpy(&config.usb, &usb, sizeof(usb));
    config.autorun_grace_ms = AUTORUN_GRACE_MS;
    config.offline_reset_triggered = offline_reset_triggered;
    config.uptime_ms = UptimeMs;
    status = M032Application_Init(&application, &config);
    if (status != PLP_OK)
    {
        for (;;)
        {
        }
    }
    status = M032NumicroHidPort_Init(&hid, &application);
    if (status != PLP_OK)
    {
        for (;;)
        {
        }
    }
    g_hirc_trim_initial = M32(HIRC_TRIM_INITIAL_REGISTER);
    for (;;)
    {
        UsbTrimProcess();
        M032Application_Process(&application, g_milliseconds);
        M032IapControl_Process(&iap_control, g_milliseconds, hid.in_busy);
    }
}

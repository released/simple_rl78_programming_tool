#include <string.h>

#include "NuMicro.h"
#include "nulink2me_application.h"
#include "nulink2me_hsusbd_port.h"
#include "nulink2me_iap_control.h"
#include "nulink2me_spim_storage.h"
#include "nulink2me_target_binding.h"

static volatile uint32_t g_milliseconds;
static uint8_t g_target_image_cache[NULINK2ME_IMAGE_CACHE_CAPACITY];

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
    uint32_t cycles_per_microsecond;
    uint32_t requested_cycles;
    uint32_t start;
    cycles_per_microsecond = SystemCoreClock / 1000000u;
    requested_cycles = cycles_per_microsecond * microseconds;
    start = DWT->CYCCNT;
    while ((uint32_t)(DWT->CYCCNT - start) < requested_cycles)
    {
    }
}

static void SystemInitialize(void)
{
    volatile uint32_t delay;
    SYS_UnlockReg();
    PF->MODE &= ~(GPIO_MODE_MODE2_Msk | GPIO_MODE_MODE3_Msk);
    CLK_EnableXtalRC(CLK_PWRCTL_HXTEN_Msk);
    CLK_WaitClockReady(CLK_STATUS_HXTSTB_Msk);
    CLK_SetHCLK(CLK_CLKSEL0_HCLKSEL_HXT, CLK_CLKDIV0_HCLK(1));
    CLK_SetCoreClock(FREQ_192MHZ);
    CLK->PCLKDIV = CLK_PCLKDIV_APB0DIV_DIV2 |
                   CLK_PCLKDIV_APB1DIV_DIV2;
    SYS->USBPHY &= ~SYS_USBPHY_HSUSBROLE_Msk;
    SYS->USBPHY =
        (SYS->USBPHY & ~(SYS_USBPHY_HSUSBROLE_Msk |
                         SYS_USBPHY_HSUSBACT_Msk)) |
        SYS_USBPHY_HSUSBEN_Msk;
    for (delay = 0u; delay < 0x1000u; ++delay)
    {
    }
    SYS->USBPHY |= SYS_USBPHY_HSUSBACT_Msk;
    CLK_EnableModuleClock(HSUSBD_MODULE);
    SystemCoreClockUpdate();
    SYS_LockReg();
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    (void)SysTick_Config(SystemCoreClock / 1000u);
}

static void FatalStorageError(Nulink2MeBoard *board)
{
    for (;;)
    {
        Nulink2MeBoard_SetLeds(board, NULINK2ME_LED_RED |
                                      NULINK2ME_LED_ISP);
        DelayUs(250000u);
        Nulink2MeBoard_SetLeds(board, 0u);
        DelayUs(250000u);
    }
}

int main(void)
{
    static Nulink2MeApplication application;
    Nulink2MeApplicationConfig config;
    Nulink2MeBoard board;
    Nulink2MeHsusbdPort hid;
    Nulink2MeSpimStorage spim_storage;
    Nulink2MeIapControl iap_control;
    Nulink2MeUsbPortOps usb;
    ProgrammerTargetOps target_ops_storage;
    ProgrammerBootOps boot;
    const ProgrammerTargetOps *target_ops;
    uint32_t status;
#if NULINK2ME_RL78_TARGET_APPROVED
    Nulink2MeRl78Target rl78_target;
#endif

    SystemInitialize();
    status = Nulink2MeBoard_Init(&board, DelayUs);
    if (status != PLP_OK)
    {
        for (;;)
        {
        }
    }
    status = Nulink2MeSpimStorage_Init(&spim_storage);
    if (status != PLP_OK)
    {
        FatalStorageError(&board);
    }
    target_ops = 0;
    memset(&target_ops_storage, 0, sizeof(target_ops_storage));
#if NULINK2ME_RL78_TARGET_APPROVED
    status = Nulink2MeRl78Target_Install(&rl78_target, &board,
                                         UptimeMs, 0,
                                         &target_ops_storage);
    if (status == PLP_OK && target_ops_storage.profile_ready != 0u)
    {
        target_ops = &target_ops_storage;
    }
#endif
    memset(&application, 0, sizeof(application));
    memset(&hid, 0, sizeof(hid));
    memset(&usb, 0, sizeof(usb));
    Nulink2MeHsusbdPort_GetOps(&hid, &usb);
    memset(&config, 0, sizeof(config));
    memset(&boot, 0, sizeof(boot));
    Nulink2MeIapControl_Init(&iap_control, UptimeMs, 0);
    Nulink2MeIapControl_GetOps(&iap_control, &boot);
    config.storage_backend = &spim_storage.backend;
    config.target = target_ops;
    config.board = &board;
    memcpy(&config.usb, &usb, sizeof(usb));
    config.capabilities = PLP_CAP_TARGET_PROGRAM;
    memcpy(&config.boot, &boot, sizeof(boot));
    config.uptime_ms = UptimeMs;
    config.image_cache_buffer = g_target_image_cache;
    config.image_cache_capacity = sizeof(g_target_image_cache);
    status = Nulink2MeApplication_Init(&application, &config);
    if (status != PLP_OK)
    {
        for (;;)
        {
        }
    }
    status = Nulink2MeHsusbdPort_Init(&hid, &application);
    if (status != PLP_OK)
    {
        for (;;)
        {
        }
    }
    for (;;)
    {
        Nulink2MeApplication_Process(&application, g_milliseconds);
        Nulink2MeIapControl_Process(&iap_control, g_milliseconds,
                                    hid.in_busy);
    }
}

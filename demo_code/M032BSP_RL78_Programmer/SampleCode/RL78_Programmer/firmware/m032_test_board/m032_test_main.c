#include <string.h>

#include "NuMicro.h"
#include "m032_application.h"
#include "m032_iap_control.h"
#include "m032_numicro_fmc_storage.h"
#include "m032_rl78_target_binding.h"
#include "m032_test_board.h"
#include "m032_test_board_profile.h"
#include "m032_test_uart_port.h"

#define AUTORUN_GRACE_MS 2000u
#define DELAY_TIMER_CLOCK_HZ 48000000u
#define DELAY_TIMER_TICKS_PER_US \
    (DELAY_TIMER_CLOCK_HZ / 1000000u)
#define DELAY_TIMER_MAX_US \
    (TIMER_CMP_MAX_VALUE / DELAY_TIMER_TICKS_PER_US)

static volatile uint32_t g_milliseconds;

static const M032ProgrammerIdentity g_test_board_identity =
{
    "M032 TEST BOARD",
    {
        0x4Du, 0x30u, 0x33u, 0x32u, 0x54u, 0x45u, 0x53u, 0x54u,
        0x80u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u
    },
    {
        0x4Du, 0x30u, 0x33u, 0x32u, 0x42u, 0x4Fu, 0x4Eu, 0x54u,
        0x80u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u
    },
    {1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
    {1u, 0u, 0u, 0u, 15u, 0u, 0u, 0u}
};

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
    uint32_t chunk_us;
    uint32_t compare_ticks;
    while (microseconds != 0u)
    {
        chunk_us = microseconds;
        if (chunk_us > DELAY_TIMER_MAX_US)
        {
            chunk_us = DELAY_TIMER_MAX_US;
        }
        compare_ticks = chunk_us * DELAY_TIMER_TICKS_PER_US;
        TIMER1->CTL = 0u;
        TIMER1->EXTCTL = 0u;
        TIMER1->INTSTS = TIMER_INTSTS_TIF_Msk;
        TIMER1->CMP = compare_ticks;
        TIMER1->CTL = TIMER_ONESHOT_MODE | TIMER_CTL_CNTEN_Msk;
        while ((TIMER1->INTSTS & TIMER_INTSTS_TIF_Msk) == 0u)
        {
        }
        TIMER1->CTL = 0u;
        TIMER1->INTSTS = TIMER_INTSTS_TIF_Msk;
        microseconds -= chunk_us;
    }
}

static void SystemInitialize(void)
{
    SYS_UnlockReg();
    CLK_EnableXtalRC(CLK_PWRCTL_HIRCEN_Msk);
    CLK_WaitClockReady(CLK_STATUS_HIRCSTB_Msk);
    CLK_SetHCLK(CLK_CLKSEL0_HCLKSEL_HIRC,
                CLK_CLKDIV0_HCLK(1));
    CLK_EnableModuleClock(TMR1_MODULE);
    CLK_SetModuleClock(TMR1_MODULE, CLK_CLKSEL1_TMR1SEL_HIRC,
                       0u);
    SYS_ResetModule(TMR1_RST);
    SystemCoreClockUpdate();
    SYS_LockReg();
    (void)SysTick_Config(SystemCoreClock / 1000u);
}

static void UpdateIndicator(M032TestBoard *board,
                            const ProgrammerCore *core,
                            uint32_t now_ms)
{
    uint8_t leds;
    leds = 0u;
    if (core->operation_state == PLP_OPERATION_FAILED ||
        core->operation_state == PLP_OPERATION_CANCELLED ||
        core->terminal_result == PLP_TERMINAL_FAIL ||
        core->terminal_result == PLP_TERMINAL_CANCELLED ||
        core->terminal_result == PLP_TERMINAL_OUTCOME_UNKNOWN)
    {
        leds = M032_TEST_LED_RED;
    }
    else if (core->operation_state == PLP_OPERATION_RUNNING ||
             core->state == PLP_STATE_PROGRAMMING)
    {
        if (((now_ms / 125u) & 1u) == 0u)
        {
            leds = M032_TEST_LED_RED;
        }
    }
    else if (core->terminal_result == PLP_TERMINAL_PASS)
    {
        leds = M032_TEST_LED_GREEN;
    }
    else if (((now_ms / 1000u) & 1u) == 0u)
    {
        leds = M032_TEST_LED_GREEN;
    }
    M032TestBoard_SetLeds(board, leds);
}

int main(void)
{
    static M032Application application;
    static M032ApplicationConfig config;
    static M032TestBoard board;
    static M032TestUartPort uart_port;
    static M032NumicroFmcStorage fmc_storage;
    static M032IapControl iap_control;
    static M032LinkPortOps link;
    static M032Rl78LinkOps rl78_link;
    static M032Rl78Target rl78_target;
    static ProgrammerTargetOps target;
    static ProgrammerBootOps boot;
    uint32_t status;

    SystemInitialize();
    status = M032TestBoard_Init(&board, DelayUs);
    if (status != PLP_OK)
    {
        for (;;)
        {
        }
    }
    status = M032NumicroFmcStorage_Init(
        &fmc_storage, M032_TEST_STORAGE_BASE,
        M032_TEST_STORAGE_SIZE, M032_TEST_METADATA_BASE,
        M032_TEST_APROM_SIZE);
    if (status != PLP_OK)
    {
        for (;;)
        {
            M032TestBoard_SetLeds(&board, M032_TEST_LED_RED);
        }
    }

    memset(&rl78_link, 0, sizeof(rl78_link));
    M032TestBoard_GetRl78LinkOps(&board, &rl78_link);
    memset(&target, 0, sizeof(target));
    status = M032Rl78Target_InstallLink(
        &rl78_target, &rl78_link, UptimeMs, 0, &target);
    if (status != PLP_OK)
    {
        for (;;)
        {
        }
    }

    memset(&application, 0, sizeof(application));
    memset(&uart_port, 0, sizeof(uart_port));
    M032TestUartPort_GetOps(&uart_port, &link);
    memset(&boot, 0, sizeof(boot));
    M032IapControl_Init(&iap_control, UptimeMs, 0);
    M032IapControl_GetOps(&iap_control, &boot);
    memset(&config, 0, sizeof(config));
    config.storage_backend = &fmc_storage.backend;
    config.target = &target;
    config.board_io = &board.io;
    config.identity = &g_test_board_identity;
    memcpy(&config.link, &link, sizeof(link));
    memcpy(&config.boot, &boot, sizeof(boot));
    config.capabilities = PLP_CAP_TARGET_PROGRAM;
    config.autorun_grace_ms = AUTORUN_GRACE_MS;
    config.offline_reset_triggered = 0u;
    config.uptime_ms = UptimeMs;
    status = M032Application_Init(&application, &config);
    if (status != PLP_OK)
    {
        for (;;)
        {
        }
    }
    status = M032TestUartPort_Init(&uart_port, &application);
    if (status != PLP_OK)
    {
        for (;;)
        {
        }
    }

    for (;;)
    {
        M032TestUartPort_Process(&uart_port);
        if (M032TestBoard_PollOfflineButton(
                &board, g_milliseconds) != 0u)
        {
            (void)M032Programmer_OnOfflineButton(
                &application.programmer);
        }
        M032Application_Process(&application, g_milliseconds);
        UpdateIndicator(&board, &application.programmer.core,
                        g_milliseconds);
        M032IapControl_Process(&iap_control, g_milliseconds,
                              uart_port.tx_busy);
    }
}

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
    {1u, 0u, 0u, 0u, 1u, 0u, 0u, 0u}
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
    CLK_SysTickDelay(microseconds);
}

static void SystemInitialize(void)
{
    SYS_UnlockReg();
    CLK_EnableXtalRC(CLK_PWRCTL_HIRCEN_Msk);
    CLK_WaitClockReady(CLK_STATUS_HIRCSTB_Msk);
    CLK_SetHCLK(CLK_CLKSEL0_HCLKSEL_HIRC,
                CLK_CLKDIV0_HCLK(1));
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
        core->terminal_result == PLP_TERMINAL_FAIL ||
        core->terminal_result == PLP_TERMINAL_OUTCOME_UNKNOWN)
    {
        if (((now_ms / 250u) & 1u) == 0u)
        {
            leds = M032_TEST_LED_RED;
        }
    }
    else if (core->operation_state == PLP_OPERATION_RUNNING ||
             core->state == PLP_STATE_PROGRAMMING)
    {
        if (((now_ms / 125u) & 1u) == 0u)
        {
            leds = M032_TEST_LED_GREEN;
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
    M032ApplicationConfig config;
    M032TestBoard board;
    M032TestUartPort uart_port;
    M032NumicroFmcStorage fmc_storage;
    M032IapControl iap_control;
    M032LinkPortOps link;
    M032Rl78LinkOps rl78_link;
    M032Rl78Target rl78_target;
    ProgrammerTargetOps target;
    ProgrammerBootOps boot;
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

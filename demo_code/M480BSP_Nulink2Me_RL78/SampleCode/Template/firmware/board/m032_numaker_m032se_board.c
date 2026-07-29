#include "m032_numaker_m032se_board.h"

#include <string.h>

#include "NuMicro.h"
#include "rl78_programmer/plp_contract.h"

static void BoardDelay(void *context, uint32_t microseconds)
{
    M032NumakerBoard *board;
    board = (M032NumakerBoard *)context;
    board->delay_us(microseconds);
}

static void SelectTool0Gpio(void)
{
    SYS_UnlockReg();
    SYS->GPA_MFPL =
        (SYS->GPA_MFPL & ~(SYS_GPA_MFPL_PA2MFP_Msk |
                           SYS_GPA_MFPL_PA3MFP_Msk)) |
        SYS_GPA_MFPL_PA2MFP_GPIO |
        SYS_GPA_MFPL_PA3MFP_GPIO;
    SYS_LockReg();
}

static uint32_t HardwareInitialize(void *context)
{
    (void)context;

    SYS_UnlockReg();

    CLK_EnableXtalRC(CLK_PWRCTL_HIRCEN_Msk);
    CLK_WaitClockReady(CLK_STATUS_HIRCSTB_Msk);

    CLK_EnableModuleClock(UART1_MODULE);
    CLK_SetModuleClock(UART1_MODULE, CLK_CLKSEL1_UART1SEL_HIRC,
                       CLK_CLKDIV0_UART1(1));
    SYS_ResetModule(UART1_RST);

    /* TOOL0/UART pins are configured first and start released. */
    SYS->GPA_MFPL =
        (SYS->GPA_MFPL & ~(SYS_GPA_MFPL_PA2MFP_Msk |
                           SYS_GPA_MFPL_PA3MFP_Msk)) |
        SYS_GPA_MFPL_PA2MFP_GPIO |
        SYS_GPA_MFPL_PA3MFP_GPIO;
    PA->DOUT |= BIT2 | BIT3;
    GPIO_SetMode(PA, BIT2 | BIT3, GPIO_MODE_INPUT);

    /* RESET is active-low open-drain and starts released. */
    SYS->GPA_MFPL =
        (SYS->GPA_MFPL & ~SYS_GPA_MFPL_PA5MFP_Msk) |
        SYS_GPA_MFPL_PA5MFP_GPIO;
    PA->DOUT |= BIT5;
    GPIO_SetMode(PA, BIT5, GPIO_MODE_OPEN_DRAIN);

    /* VDD enable starts disabled; PB.3 target-VDD sense remains unused. */
    SYS->GPA_MFPL =
        (SYS->GPA_MFPL & ~SYS_GPA_MFPL_PA4MFP_Msk) |
        SYS_GPA_MFPL_PA4MFP_GPIO;
    PA->DOUT &= ~BIT4;
    GPIO_SetMode(PA, BIT4, GPIO_MODE_OUTPUT);

    /* The only MCU-controlled EVB LED is PB.14 and is active-low. */
    SYS->GPB_MFPH =
        (SYS->GPB_MFPH & ~SYS_GPB_MFPH_PB14MFP_Msk) |
        SYS_GPB_MFPH_PB14MFP_GPIO;
    PB->DOUT |= BIT14;
    GPIO_SetMode(PB, BIT14, GPIO_MODE_OUTPUT);

    SYS_LockReg();

    return PLP_OK;
}

static uint32_t HardwareSetTool0Low(void *context, uint8_t drive_low)
{
    (void)context;
    UART_Close(UART1);
    SelectTool0Gpio();
    if (drive_low != 0u)
    {
        PA->DOUT &= ~BIT3;
        GPIO_SetMode(PA, BIT3, GPIO_MODE_OPEN_DRAIN);
    }
    else
    {
        PA->DOUT |= BIT3;
        GPIO_SetMode(PA, BIT3, GPIO_MODE_INPUT);
    }
    return PLP_OK;
}

static uint32_t HardwareSetUartMode(void *context, uint8_t dedicated_uart)
{
    (void)context;
    PA->DOUT |= BIT2 | BIT3;
    GPIO_SetMode(PA, BIT2 | BIT3, GPIO_MODE_INPUT);
    SYS_UnlockReg();
    if (dedicated_uart != 0u)
    {
        SYS->GPA_MFPL =
            (SYS->GPA_MFPL & ~(SYS_GPA_MFPL_PA2MFP_Msk |
                               SYS_GPA_MFPL_PA3MFP_Msk)) |
            SYS_GPA_MFPL_PA2MFP_UART1_RXD |
            SYS_GPA_MFPL_PA3MFP_UART1_TXD;
    }
    else
    {
        SYS->GPA_MFPL =
            (SYS->GPA_MFPL & ~(SYS_GPA_MFPL_PA2MFP_Msk |
                               SYS_GPA_MFPL_PA3MFP_Msk)) |
            SYS_GPA_MFPL_PA2MFP_GPIO |
            SYS_GPA_MFPL_PA3MFP_UART1_TXD;
    }
    SYS_LockReg();
    UART_Open(UART1, M032_BOARD_UART_BAUD);
    UART_SetLine_Config(UART1, M032_BOARD_UART_BAUD, UART_WORD_LEN_8,
                        UART_PARITY_ODD, UART_STOP_BIT_2);
    if (dedicated_uart == 0u)
    {
        UART_SelectSingleWireMode(UART1);
    }
    return PLP_OK;
}

static uint32_t HardwareSetReset(void *context, uint8_t asserted)
{
    (void)context;
    if (asserted != 0u)
    {
        PA->DOUT &= ~BIT5;
    }
    else
    {
        PA->DOUT |= BIT5;
    }
    return PLP_OK;
}

static uint32_t HardwareSetTargetPower(void *context, uint8_t enabled)
{
    (void)context;
    if (enabled != 0u)
    {
        PA->DOUT |= BIT4;
    }
    else
    {
        PA->DOUT &= ~BIT4;
    }
    return PLP_OK;
}

static uint32_t HardwareSetStatusLed(void *context, uint8_t on)
{
    (void)context;
    if (on != 0u)
    {
        PB->DOUT &= ~BIT14;
    }
    else
    {
        PB->DOUT |= BIT14;
    }
    return PLP_OK;
}

uint32_t M032NumakerBoard_Init(M032NumakerBoard *board,
                               M032NumakerDelayUs delay_us)
{
    M032BoardIoOps ops;
    uint32_t status;
    if (board == 0 || delay_us == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    memset(board, 0, sizeof(*board));
    board->delay_us = delay_us;
    memset(&ops, 0, sizeof(ops));
    ops.context = board;
    ops.initialize = HardwareInitialize;
    ops.set_tool0_low = HardwareSetTool0Low;
    ops.set_uart_mode = HardwareSetUartMode;
    ops.set_reset = HardwareSetReset;
    ops.set_target_power = HardwareSetTargetPower;
    ops.set_status_led = HardwareSetStatusLed;
    ops.delay_us = BoardDelay;
    ops.target_vdd_sense_enabled = M032_BOARD_TARGET_VDD_SENSE_ENABLED;
    status = M032BoardIo_Init(&board->io, &ops);
    if (status == PLP_OK)
    {
        board->initialized = 1u;
    }
    return status;
}

uint32_t M032NumakerBoard_TryWriteByte(M032NumakerBoard *board, uint8_t value)
{
    if (board == 0 || board->initialized == 0u ||
        board->io.programming_mode == 0u)
    {
        return PLP_ERR_DRIVER_PROFILE;
    }
    if (UART_GET_TX_FULL(UART1) != 0u)
    {
        return PLP_ERR_SYSTEM_RESOURCE;
    }
    UART_WRITE(UART1, value);
    return PLP_OK;
}

uint32_t M032NumakerBoard_TryReadByte(M032NumakerBoard *board, uint8_t *value)
{
    if (board == 0 || value == 0 || board->initialized == 0u ||
        board->io.programming_mode == 0u)
    {
        return PLP_ERR_DRIVER_PROFILE;
    }
    if (UART_GET_RX_EMPTY(UART1) != 0u)
    {
        return PLP_ERR_SYSTEM_RESOURCE;
    }
    *value = (uint8_t)UART_READ(UART1);
    return PLP_OK;
}

uint8_t M032NumakerBoard_IsTransmitIdle(const M032NumakerBoard *board)
{
    if (board == 0 || board->initialized == 0u)
    {
        return 0u;
    }
    return UART_IS_TX_EMPTY(UART1) != 0u ? 1u : 0u;
}

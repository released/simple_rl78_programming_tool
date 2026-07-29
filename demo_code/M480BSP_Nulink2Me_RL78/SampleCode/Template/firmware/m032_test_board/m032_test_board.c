#include "m032_test_board.h"

#include <string.h>

#include "NuMicro.h"
#include "m032_test_board_profile.h"
#include "rl78_programmer/plp_contract.h"

static void BoardDelay(void *context, uint32_t microseconds)
{
    M032TestBoard *board;
    board = (M032TestBoard *)context;
    board->delay_us(microseconds);
}

void M032TestBoard_SetLeds(M032TestBoard *board, uint8_t led_mask)
{
    uint32_t output;
    if (board == 0)
    {
        return;
    }
    output = PA->DOUT | BIT10 | BIT11;
    if ((led_mask & M032_TEST_LED_RED) != 0u)
    {
        output &= ~BIT11;
    }
    if ((led_mask & M032_TEST_LED_GREEN) != 0u)
    {
        output &= ~BIT10;
    }
    PA->DOUT = output;
    board->led_mask = led_mask;
}

static uint32_t HardwareInitialize(void *context)
{
    M032TestBoard *board;
    uint32_t status;
    board = (M032TestBoard *)context;
    status = M032TestBoardProfile_Validate();
    if (status != PLP_OK)
    {
        return status;
    }
    SYS_UnlockReg();
    SYS->GPB_MFPH =
        (SYS->GPB_MFPH & ~(SYS_GPB_MFPH_PB8MFP_Msk |
                           SYS_GPB_MFPH_PB9MFP_Msk |
                           SYS_GPB_MFPH_PB10MFP_Msk)) |
        SYS_GPB_MFPH_PB8MFP_GPIO |
        SYS_GPB_MFPH_PB9MFP_GPIO |
        SYS_GPB_MFPH_PB10MFP_GPIO;
    PB->DOUT |= BIT8 | BIT9 | BIT10;
    GPIO_SetMode(PB, BIT8 | BIT10, GPIO_MODE_OPEN_DRAIN);
    GPIO_SetMode(PB, BIT9, GPIO_MODE_INPUT);

    SYS->GPC_MFPL =
        (SYS->GPC_MFPL & ~SYS_GPC_MFPL_PC2MFP_Msk) |
        SYS_GPC_MFPL_PC2MFP_GPIO;
    GPIO_SetMode(PC, BIT2, GPIO_MODE_INPUT);

    SYS->GPA_MFPH =
        (SYS->GPA_MFPH & ~(SYS_GPA_MFPH_PA10MFP_Msk |
                           SYS_GPA_MFPH_PA11MFP_Msk)) |
        SYS_GPA_MFPH_PA10MFP_GPIO |
        SYS_GPA_MFPH_PA11MFP_GPIO;
    PA->DOUT |= BIT10 | BIT11;
    GPIO_SetMode(PA, BIT10 | BIT11, GPIO_MODE_OUTPUT);
    SYS_LockReg();

    status = M032TestTool0SoftUart_Init(&board->tool0_uart);
    board->button_raw = M032TestBoard_IsButtonPressed();
    board->button_stable = board->button_raw;
    M032TestBoard_SetLeds(board, 0u);
    return status;
}

static uint32_t HardwareSetTool0Low(void *context, uint8_t drive_low)
{
    M032TestBoard *board;
    board = (M032TestBoard *)context;
    return M032TestTool0SoftUart_SetTool0Low(&board->tool0_uart,
                                             drive_low);
}

static uint32_t HardwareSetUartMode(void *context,
                                   uint8_t dedicated_uart)
{
    M032TestBoard *board;
    board = (M032TestBoard *)context;
    if (dedicated_uart != 0u)
    {
        return PLP_ERR_DRIVER_PROFILE;
    }
    return M032TestTool0SoftUart_SetProgrammingMode(
        &board->tool0_uart, 1u);
}

static uint32_t HardwareSetReset(void *context, uint8_t asserted)
{
    (void)context;
    if (asserted != 0u)
    {
        PB->DOUT &= ~BIT8;
    }
    else
    {
        PB->DOUT |= BIT8;
    }
    return PLP_OK;
}

static uint32_t HardwareSetTargetPower(void *context, uint8_t enabled)
{
    (void)context;
    (void)enabled;
    return PLP_OK;
}

static uint32_t HardwareSetStatusLed(void *context, uint8_t on)
{
    M032TestBoard *board;
    uint8_t mask;
    board = (M032TestBoard *)context;
    mask = board->led_mask;
    if (on != 0u)
    {
        mask |= M032_TEST_LED_GREEN;
    }
    else
    {
        mask &= (uint8_t)~M032_TEST_LED_GREEN;
    }
    M032TestBoard_SetLeds(board, mask);
    return PLP_OK;
}

uint32_t M032TestBoard_Init(M032TestBoard *board,
                            M032TestDelayUs delay_us)
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
    ops.target_vdd_sense_enabled = 0u;
    status = M032BoardIo_Init(&board->io, &ops);
    if (status == PLP_OK)
    {
        board->initialized = 1u;
    }
    return status;
}

uint8_t M032TestBoard_IsButtonPressed(void)
{
    return (PC->PIN & BIT2) == 0u ? 1u : 0u;
}

uint8_t M032TestBoard_PollOfflineButton(M032TestBoard *board,
                                        uint32_t now_ms)
{
    uint8_t pressed;
    if (board == 0 || board->initialized == 0u)
    {
        return 0u;
    }
    pressed = M032TestBoard_IsButtonPressed();
    if (pressed != board->button_raw)
    {
        board->button_raw = pressed;
        board->button_change_ms = now_ms;
        return 0u;
    }
    if (pressed != board->button_stable &&
        (uint32_t)(now_ms - board->button_change_ms) >=
            M032_TEST_BUTTON_DEBOUNCE_MS)
    {
        board->button_stable = pressed;
        return pressed;
    }
    return 0u;
}

static uint32_t PrepareAttempt(void *context)
{
    M032TestBoard *board;
    board = (M032TestBoard *)context;
    return M032TestTool0SoftUart_PrepareAttempt(
        &board->tool0_uart);
}

static uint32_t TryWriteByte(void *context, uint8_t value)
{
    M032TestBoard *board;
    board = (M032TestBoard *)context;
    if (board == 0 || board->initialized == 0u ||
        board->io.programming_mode == 0u)
    {
        return PLP_ERR_DRIVER_PROFILE;
    }
    return M032TestTool0SoftUart_TryWriteByte(
        &board->tool0_uart, value);
}

static uint32_t TryReadByte(void *context, uint8_t *value)
{
    M032TestBoard *board;
    board = (M032TestBoard *)context;
    if (board == 0 || value == 0 || board->initialized == 0u ||
        board->io.programming_mode == 0u)
    {
        return PLP_ERR_DRIVER_PROFILE;
    }
    return M032TestTool0SoftUart_TryReadByte(
        &board->tool0_uart, value);
}

static uint8_t IsTransmitIdle(void *context)
{
    M032TestBoard *board;
    board = (M032TestBoard *)context;
    if (board == 0 || board->initialized == 0u)
    {
        return 0u;
    }
    return M032TestTool0SoftUart_IsTransmitIdle(
        &board->tool0_uart);
}

static uint32_t GetFailureSnapshot(
    void *context, ProgrammerTargetFailureSnapshot *snapshot)
{
    M032TestBoard *board;
    board = (M032TestBoard *)context;
    return M032TestTool0SoftUart_GetFailureSnapshot(
        &board->tool0_uart, snapshot);
}

void M032TestBoard_GetRl78LinkOps(M032TestBoard *board,
                                  M032Rl78LinkOps *ops)
{
    if (board == 0 || ops == 0)
    {
        return;
    }
    memset(ops, 0, sizeof(*ops));
    ops->context = board;
    ops->board_io = &board->io;
    ops->prepare_attempt = PrepareAttempt;
    ops->try_write_byte = TryWriteByte;
    ops->try_read_byte = TryReadByte;
    ops->is_transmit_idle = IsTransmitIdle;
    ops->get_failure_snapshot = GetFailureSnapshot;
}

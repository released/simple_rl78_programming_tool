#include "nulink2me_board.h"

#include <string.h>

#include "NuMicro.h"
#include "nulink2me_board_profile.h"
#include "rl78_programmer/plp_contract.h"

static void BoardDelay(void *context, uint32_t microseconds)
{
    Nulink2MeBoard *board;
    board = (Nulink2MeBoard *)context;
    board->delay_us(microseconds);
}

void Nulink2MeBoard_SetLeds(Nulink2MeBoard *board, uint8_t led_mask)
{
    uint32_t outputs;
    if (board == 0)
    {
        return;
    }
    outputs = PF->DOUT | BIT4;
    if ((led_mask & NULINK2ME_LED_ICE) != 0u)
    {
        outputs &= ~BIT4;
    }
    PF->DOUT = outputs;

    outputs = PC->DOUT | BIT6;
    if ((led_mask & NULINK2ME_LED_ISP) != 0u)
    {
        outputs &= ~BIT6;
    }
    PC->DOUT = outputs;

    outputs = PB->DOUT | BIT8 | BIT9;
    if ((led_mask & NULINK2ME_LED_GREEN) != 0u)
    {
        outputs &= ~BIT8;
    }
    if ((led_mask & NULINK2ME_LED_RED) != 0u)
    {
        outputs &= ~BIT9;
    }
    PB->DOUT = outputs;
    board->led_mask = led_mask;
}

static uint32_t HardwareInitialize(void *context)
{
    Nulink2MeBoard *board;
    uint32_t status;
    board = (Nulink2MeBoard *)context;

    SYS_UnlockReg();
    SYS->GPF_MFPL =
        (SYS->GPF_MFPL & ~(SYS_GPF_MFPL_PF4MFP_Msk |
                           SYS_GPF_MFPL_PF5MFP_Msk)) |
        SYS_GPF_MFPL_PF4MFP_GPIO |
        SYS_GPF_MFPL_PF5MFP_GPIO;
    PF->DOUT |= BIT4 | BIT5;
    GPIO_SetMode(PF, BIT4, GPIO_MODE_OUTPUT);
    GPIO_SetMode(PF, BIT5, GPIO_MODE_OPEN_DRAIN);

    SYS->GPC_MFPL =
        (SYS->GPC_MFPL & ~(SYS_GPC_MFPL_PC6MFP_Msk |
                           SYS_GPC_MFPL_PC7MFP_Msk)) |
        SYS_GPC_MFPL_PC6MFP_GPIO |
        SYS_GPC_MFPL_PC7MFP_GPIO;
    PC->DOUT |= BIT6 | BIT7;
    GPIO_SetMode(PC, BIT6, GPIO_MODE_OUTPUT);
    GPIO_SetMode(PC, BIT7, GPIO_MODE_INPUT);

    SYS->GPB_MFPH =
        (SYS->GPB_MFPH & ~(SYS_GPB_MFPH_PB8MFP_Msk |
                           SYS_GPB_MFPH_PB9MFP_Msk)) |
        SYS_GPB_MFPH_PB8MFP_GPIO |
        SYS_GPB_MFPH_PB9MFP_GPIO;
    PB->DOUT |= BIT8 | BIT9;
    GPIO_SetMode(PB, BIT8 | BIT9, GPIO_MODE_OUTPUT);
    SYS_LockReg();

    status = Nulink2MeTool0SoftUart_Init(&board->tool0_uart);
    if (status != PLP_OK)
    {
        return status;
    }
    board->button_raw = (PC->PIN & BIT7) == 0u ? 1u : 0u;
    board->button_stable = board->button_raw;
    Nulink2MeBoard_SetLeds(board, 0u);
    return PLP_OK;
}

static uint32_t HardwareSetTool0Low(void *context, uint8_t drive_low)
{
    Nulink2MeBoard *board;
    board = (Nulink2MeBoard *)context;
    return Nulink2MeTool0SoftUart_SetTool0Low(&board->tool0_uart,
                                              drive_low);
}

static uint32_t HardwareSetUartMode(void *context, uint8_t dedicated_uart)
{
    Nulink2MeBoard *board;
    board = (Nulink2MeBoard *)context;
    if (dedicated_uart != 0u)
    {
        return PLP_ERR_DRIVER_PROFILE;
    }
    return Nulink2MeTool0SoftUart_SetProgrammingMode(&board->tool0_uart, 1u);
}

static uint32_t HardwareSetReset(void *context, uint8_t asserted)
{
    (void)context;
    if (asserted != 0u)
    {
        PF->DOUT &= ~BIT5;
    }
    else
    {
        PF->DOUT |= BIT5;
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
    Nulink2MeBoard *board;
    uint8_t mask;
    board = (Nulink2MeBoard *)context;
    mask = board->led_mask;
    if (on != 0u)
    {
        mask |= NULINK2ME_LED_ICE;
    }
    else
    {
        mask &= (uint8_t)~NULINK2ME_LED_ICE;
    }
    Nulink2MeBoard_SetLeds(board, mask);
    return PLP_OK;
}

uint32_t Nulink2MeBoard_ValidateProfile(void)
{
    return Nulink2MeBoardProfile_Validate();
}

uint32_t Nulink2MeBoard_Init(Nulink2MeBoard *board,
                             Nulink2MeDelayUs delay_us)
{
    M032BoardIoOps ops;
    uint32_t status;
    if (board == 0 || delay_us == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    status = Nulink2MeBoard_ValidateProfile();
    if (status != PLP_OK)
    {
        return status;
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
    ops.target_vdd_sense_enabled =
        NULINK2ME_TARGET_VDD_SENSE_ENABLED;
    status = M032BoardIo_Init(&board->io, &ops);
    if (status == PLP_OK)
    {
        board->initialized = 1u;
    }
    return status;
}

uint8_t Nulink2MeBoard_PollOfflineButton(Nulink2MeBoard *board,
                                         uint32_t now_ms)
{
    uint8_t pressed;
    if (board == 0 || board->initialized == 0u)
    {
        return 0u;
    }
    pressed = (PC->PIN & BIT7) == 0u ? 1u : 0u;
    if (pressed != board->button_raw)
    {
        board->button_raw = pressed;
        board->button_change_ms = now_ms;
        return 0u;
    }
    if (pressed != board->button_stable &&
        (uint32_t)(now_ms - board->button_change_ms) >=
            NULINK2ME_BUTTON_DEBOUNCE_MS)
    {
        board->button_stable = pressed;
        return pressed;
    }
    return 0u;
}

uint32_t Nulink2MeBoard_TryWriteByte(Nulink2MeBoard *board, uint8_t value)
{
    if (board == 0 || board->initialized == 0u ||
        board->io.programming_mode == 0u)
    {
        return PLP_ERR_DRIVER_PROFILE;
    }
    return Nulink2MeTool0SoftUart_TryWriteByte(&board->tool0_uart, value);
}

uint32_t Nulink2MeBoard_TryReadByte(Nulink2MeBoard *board, uint8_t *value)
{
    if (board == 0 || value == 0 || board->initialized == 0u ||
        board->io.programming_mode == 0u)
    {
        return PLP_ERR_DRIVER_PROFILE;
    }
    return Nulink2MeTool0SoftUart_TryReadByte(&board->tool0_uart, value);
}

uint8_t Nulink2MeBoard_IsTransmitIdle(Nulink2MeBoard *board)
{
    if (board == 0 || board->initialized == 0u)
    {
        return 0u;
    }
    return Nulink2MeTool0SoftUart_IsTransmitIdle(&board->tool0_uart);
}

uint32_t Nulink2MeBoard_PrepareAttempt(Nulink2MeBoard *board)
{
    if (board == 0 || board->initialized == 0u)
    {
        return PLP_ERR_DRIVER_PROFILE;
    }
    return Nulink2MeTool0SoftUart_PrepareAttempt(&board->tool0_uart);
}

uint32_t Nulink2MeBoard_ResetProgrammingLink(Nulink2MeBoard *board)
{
    uint32_t status;
    if (board == 0 || board->initialized == 0u)
    {
        return PLP_ERR_DRIVER_PROFILE;
    }
    status = Nulink2MeBoard_ParkProgrammingLink(board);
    if (status == PLP_OK)
    {
        board->io.ops.delay_us(board->io.ops.context,
                                M032_BOARD_TOOL0_SETUP_US);
        status = board->io.ops.set_reset(board->io.ops.context, 0u);
    }
    return status;
}

uint32_t Nulink2MeBoard_ParkProgrammingLink(Nulink2MeBoard *board)
{
    uint32_t status;
    uint32_t next;
    if (board == 0 || board->initialized == 0u)
    {
        return PLP_ERR_DRIVER_PROFILE;
    }
    status = board->io.ops.set_reset(board->io.ops.context, 1u);
    next = Nulink2MeTool0SoftUart_PrepareAttempt(&board->tool0_uart);
    if (status == PLP_OK)
    {
        status = next;
    }
    board->io.programming_mode = 0u;
    return status;
}

uint32_t Nulink2MeBoard_GetFailureSnapshot(
    Nulink2MeBoard *board, Nulink2MeTool0FailureSnapshot *snapshot)
{
    if (board == 0 || snapshot == 0 || board->initialized == 0u)
    {
        return PLP_ERR_DRIVER_PROFILE;
    }
    return Nulink2MeTool0SoftUart_GetFailureSnapshot(
        &board->tool0_uart, snapshot);
}

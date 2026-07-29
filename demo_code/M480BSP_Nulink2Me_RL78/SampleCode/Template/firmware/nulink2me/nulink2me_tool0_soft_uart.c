#include "nulink2me_tool0_soft_uart.h"

#include <string.h>

#include "NuMicro.h"
#include "rl78_programmer/plp_contract.h"

#define NULINK2ME_TOOL0_TIMER_STATE_IDLE     0u
#define NULINK2ME_TOOL0_TIMER_STATE_TX       1u
#define NULINK2ME_TOOL0_TIMER_STATE_RX_START 2u
#define NULINK2ME_TOOL0_TIMER_STATE_RX_BITS  3u

#define NULINK2ME_TOOL0_TIMER_BIT_TICKS      104u
#define NULINK2ME_TOOL0_TIMER_HALF_BIT_TICKS 52u
#define NULINK2ME_TOOL0_TX_FRAME_BITS        12u
#define NULINK2ME_TOOL0_TX_FIRST_STOP_BIT    10u
#define NULINK2ME_TOOL0_RX_PARITY_BIT        8u
#define NULINK2ME_TOOL0_IRQ_PRIORITY         0u

static Nulink2MeTool0SoftUart *g_tool0_uart;

static void StopTimer(void)
{
    TIMER2->CTL = 0u;
    TIMER2->INTSTS = TIMER_INTSTS_TIF_Msk;
}

static void StartTimer(uint32_t ticks, uint32_t mode)
{
    TIMER2->CTL = 0u;
    TIMER2->CMP = ticks;
    TIMER2->INTSTS = TIMER_INTSTS_TIF_Msk;
    TIMER2->CTL = mode | TIMER_CTL_INTEN_Msk | TIMER_CTL_CNTEN_Msk;
}

static void ReleaseTransmitPin(void)
{
    PA->DOUT |= BIT9;
}

static void DriveTransmitPinLow(void)
{
    PA->DOUT &= ~BIT9;
}

static uint8_t ReadReceivePin(void)
{
    return (PA->PIN & BIT10) != 0u ? 1u : 0u;
}

static void DisableReceiveStartInterrupt(void)
{
    GPIO_DisableInt(PA, 10u);
    GPIO_CLR_INT_FLAG(PA, BIT10);
}

static void EnableReceiveStartInterrupt(Nulink2MeTool0SoftUart *uart)
{
    GPIO_CLR_INT_FLAG(PA, BIT10);
    uart->rx_enabled = 1u;
    GPIO_EnableInt(PA, 10u, GPIO_INT_FALLING);
}

static void ResetTransferState(Nulink2MeTool0SoftUart *uart)
{
    DisableReceiveStartInterrupt();
    StopTimer();
    uart->tx_frame = 0u;
    uart->current_tx_value = 0u;
    uart->rx_error = PLP_OK;
    uart->tx_bit_index = 0u;
    uart->tx_busy = 0u;
    uart->timer_state = NULINK2ME_TOOL0_TIMER_STATE_IDLE;
    uart->rx_enabled = 0u;
    uart->rx_bit_index = 0u;
    uart->rx_value = 0u;
    uart->rx_parity = 0u;
    uart->rx_frame_valid = 0u;
    uart->rx_head = 0u;
    uart->rx_tail = 0u;
}

static uint16_t BuildTransmitFrame(uint8_t value)
{
    uint16_t frame;
    uint8_t parity;
    uint8_t bit_index;
    parity = 0u;
    for (bit_index = 0u; bit_index < 8u; ++bit_index)
    {
        parity ^= (uint8_t)((value >> bit_index) & 1u);
    }
    frame = (uint16_t)((uint16_t)value << 1u);
    if (parity == 0u)
    {
        frame |= (uint16_t)(1u << 9u);
    }
    frame |= (uint16_t)((1u << 10u) | (1u << 11u));
    return frame;
}

static void DriveTransmitFrameBit(const Nulink2MeTool0SoftUart *uart)
{
    if ((uart->tx_frame & (uint16_t)(1u << uart->tx_bit_index)) != 0u)
    {
        ReleaseTransmitPin();
    }
    else
    {
        DriveTransmitPinLow();
    }
}

static void SetReceiveError(Nulink2MeTool0SoftUart *uart)
{
    if (uart->rx_error == PLP_OK)
    {
        uart->rx_error = PLP_ERR_DRIVER_TARGET_STATUS;
    }
}

static void AbortTransmitDirectionCollision(Nulink2MeTool0SoftUart *uart)
{
    uint8_t pin_flags;
    pin_flags = 0u;
    if ((PA->DOUT & BIT9) != 0u)
    {
        pin_flags |= PLP_OFFLINE_FAILURE_PIN_PA9_DOUT;
    }
    if ((PA->PIN & BIT9) != 0u)
    {
        pin_flags |= PLP_OFFLINE_FAILURE_PIN_PA9_INPUT;
    }
    if ((PA->PIN & BIT10) != 0u)
    {
        pin_flags |= PLP_OFFLINE_FAILURE_PIN_PA10_INPUT;
    }
    if (GPIO_GET_INT_FLAG(PA, BIT10) != 0u)
    {
        pin_flags |= PLP_OFFLINE_FAILURE_PIN_PA10_IRQ;
    }
    if (uart->tx_busy != 0u)
    {
        pin_flags |= PLP_OFFLINE_FAILURE_PIN_TX_BUSY;
    }
    if (uart->rx_enabled != 0u)
    {
        pin_flags |= PLP_OFFLINE_FAILURE_PIN_RX_ENABLED;
    }
    uart->failure_valid = 1u;
    uart->failure_tx_value = uart->current_tx_value;
    uart->failure_tx_bit_index = uart->tx_bit_index;
    uart->failure_timer_state = uart->timer_state;
    uart->failure_pin_flags = pin_flags;
    uart->failure_timer_count = (uint16_t)TIMER2->CNT;
    StopTimer();
    ReleaseTransmitPin();
    uart->timer_state = NULINK2ME_TOOL0_TIMER_STATE_IDLE;
    uart->tx_busy = 0u;
    if (uart->rx_error == PLP_OK)
    {
        uart->rx_error = PLP_ERR_TARGET_DIRECTION;
    }
    if (uart->rx_enabled != 0u)
    {
        EnableReceiveStartInterrupt(uart);
    }
}

static void QueueReceivedByte(Nulink2MeTool0SoftUart *uart, uint8_t value)
{
    if ((uint8_t)(uart->rx_tail - uart->rx_head) >=
        NULINK2ME_TOOL0_RX_FIFO_SIZE)
    {
        SetReceiveError(uart);
        return;
    }
    uart->rx_fifo[uart->rx_tail &
                  (NULINK2ME_TOOL0_RX_FIFO_SIZE - 1u)] = value;
    uart->rx_tail++;
}

static void FinishReceiveFrame(Nulink2MeTool0SoftUart *uart)
{
    StopTimer();
    uart->timer_state = NULINK2ME_TOOL0_TIMER_STATE_IDLE;
    if (uart->rx_frame_valid != 0u)
    {
        QueueReceivedByte(uart, uart->rx_value);
    }
    else
    {
        SetReceiveError(uart);
    }
    if (uart->rx_enabled != 0u)
    {
        EnableReceiveStartInterrupt(uart);
    }
}

static void ProcessTransmitTimer(Nulink2MeTool0SoftUart *uart)
{
    if (uart->tx_bit_index < NULINK2ME_TOOL0_TX_FIRST_STOP_BIT &&
        (uart->tx_frame & (uint16_t)(1u << uart->tx_bit_index)) == 0u &&
        ReadReceivePin() != 0u)
    {
        AbortTransmitDirectionCollision(uart);
        return;
    }
    uart->tx_bit_index++;
    if (uart->tx_bit_index >= NULINK2ME_TOOL0_TX_FRAME_BITS)
    {
        StopTimer();
        ReleaseTransmitPin();
        uart->timer_state = NULINK2ME_TOOL0_TIMER_STATE_IDLE;
        uart->tx_busy = 0u;
        return;
    }
    DriveTransmitFrameBit(uart);
    if (uart->tx_bit_index == NULINK2ME_TOOL0_TX_FIRST_STOP_BIT)
    {
        EnableReceiveStartInterrupt(uart);
    }
}

static void ProcessReceiveStartTimer(Nulink2MeTool0SoftUart *uart)
{
    if (ReadReceivePin() != 0u)
    {
        StopTimer();
        uart->timer_state = NULINK2ME_TOOL0_TIMER_STATE_IDLE;
        if (uart->rx_enabled != 0u)
        {
            EnableReceiveStartInterrupt(uart);
        }
        return;
    }
    uart->rx_bit_index = 0u;
    uart->rx_value = 0u;
    uart->rx_parity = 0u;
    uart->rx_frame_valid = 1u;
    uart->timer_state = NULINK2ME_TOOL0_TIMER_STATE_RX_BITS;
    StartTimer(NULINK2ME_TOOL0_TIMER_BIT_TICKS, TIMER_PERIODIC_MODE);
}

static void ProcessReceiveBitTimer(Nulink2MeTool0SoftUart *uart)
{
    uint8_t sample;
    sample = ReadReceivePin();
    if (uart->rx_bit_index < NULINK2ME_TOOL0_RX_PARITY_BIT)
    {
        if (sample != 0u)
        {
            uart->rx_value |= (uint8_t)(1u << uart->rx_bit_index);
        }
        uart->rx_parity ^= sample;
    }
    else if (uart->rx_bit_index == NULINK2ME_TOOL0_RX_PARITY_BIT)
    {
        if ((uint8_t)(uart->rx_parity ^ sample) == 0u)
        {
            uart->rx_frame_valid = 0u;
        }
    }
    else
    {
        if (sample == 0u)
        {
            uart->rx_frame_valid = 0u;
        }
        FinishReceiveFrame(uart);
        return;
    }
    uart->rx_bit_index++;
}

uint32_t Nulink2MeTool0SoftUart_Init(Nulink2MeTool0SoftUart *uart)
{
    if (uart == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    if (g_tool0_uart != 0 && g_tool0_uart != uart)
    {
        return PLP_ERR_SYSTEM_RESOURCE;
    }
    memset(uart, 0, sizeof(*uart));
    SYS_UnlockReg();
    CLK_EnableModuleClock(TMR2_MODULE);
    CLK_SetModuleClock(TMR2_MODULE, CLK_CLKSEL1_TMR2SEL_HXT, 0u);
    SYS_ResetModule(TMR2_RST);
    SYS->GPA_MFPH =
        (SYS->GPA_MFPH & ~(SYS_GPA_MFPH_PA9MFP_Msk |
                           SYS_GPA_MFPH_PA10MFP_Msk)) |
        SYS_GPA_MFPH_PA9MFP_GPIO |
        SYS_GPA_MFPH_PA10MFP_GPIO;
    SYS_LockReg();

    PA->DOUT |= BIT9 | BIT10;
    GPIO_SetMode(PA, BIT9, GPIO_MODE_OPEN_DRAIN);
    GPIO_SetMode(PA, BIT10, GPIO_MODE_INPUT);
    StopTimer();
    DisableReceiveStartInterrupt();
    g_tool0_uart = uart;
    uart->initialized = 1u;
    NVIC_ClearPendingIRQ(GPA_IRQn);
    NVIC_ClearPendingIRQ(TMR2_IRQn);
    NVIC_SetPriority(GPA_IRQn, NULINK2ME_TOOL0_IRQ_PRIORITY);
    NVIC_SetPriority(TMR2_IRQn, NULINK2ME_TOOL0_IRQ_PRIORITY);
    NVIC_EnableIRQ(GPA_IRQn);
    NVIC_EnableIRQ(TMR2_IRQn);
    return PLP_OK;
}

uint32_t Nulink2MeTool0SoftUart_SetTool0Low(
    Nulink2MeTool0SoftUart *uart, uint8_t drive_low)
{
    uint32_t interrupt_state;
    if (uart == 0 || uart->initialized == 0u)
    {
        return PLP_ERR_DRIVER_PROFILE;
    }
    interrupt_state = __get_PRIMASK();
    __disable_irq();
    ResetTransferState(uart);
    if (drive_low != 0u)
    {
        DriveTransmitPinLow();
    }
    else
    {
        ReleaseTransmitPin();
    }
    if (interrupt_state == 0u)
    {
        __enable_irq();
    }
    return PLP_OK;
}

uint32_t Nulink2MeTool0SoftUart_SetProgrammingMode(
    Nulink2MeTool0SoftUart *uart, uint8_t enabled)
{
    uint32_t interrupt_state;
    if (uart == 0 || uart->initialized == 0u)
    {
        return PLP_ERR_DRIVER_PROFILE;
    }
    interrupt_state = __get_PRIMASK();
    __disable_irq();
    ResetTransferState(uart);
    ReleaseTransmitPin();
    if (enabled != 0u)
    {
        EnableReceiveStartInterrupt(uart);
    }
    if (interrupt_state == 0u)
    {
        __enable_irq();
    }
    return PLP_OK;
}

uint32_t Nulink2MeTool0SoftUart_PrepareAttempt(
    Nulink2MeTool0SoftUart *uart)
{
    uint32_t interrupt_state;
    if (uart == 0 || uart->initialized == 0u)
    {
        return PLP_ERR_DRIVER_PROFILE;
    }
    interrupt_state = __get_PRIMASK();
    __disable_irq();
    ReleaseTransmitPin();
    DisableReceiveStartInterrupt();
    StopTimer();
    SYS_UnlockReg();
    SYS_ResetModule(TMR2_RST);
    SYS_LockReg();
    ResetTransferState(uart);
    NVIC_ClearPendingIRQ(GPA_IRQn);
    NVIC_ClearPendingIRQ(TMR2_IRQn);
    uart->failure_valid = 0u;
    uart->failure_tx_value = 0u;
    uart->failure_tx_bit_index = 0u;
    uart->failure_timer_state = NULINK2ME_TOOL0_TIMER_STATE_IDLE;
    uart->failure_pin_flags = 0u;
    uart->failure_timer_count = 0u;
    if (interrupt_state == 0u)
    {
        __enable_irq();
    }
    return PLP_OK;
}

uint32_t Nulink2MeTool0SoftUart_TryWriteByte(
    Nulink2MeTool0SoftUart *uart, uint8_t value)
{
    uint32_t error;
    uint32_t interrupt_state;
    if (uart == 0 || uart->initialized == 0u || uart->rx_enabled == 0u)
    {
        return PLP_ERR_DRIVER_PROFILE;
    }
    interrupt_state = __get_PRIMASK();
    __disable_irq();
    error = uart->rx_error;
    if (error != PLP_OK)
    {
        uart->rx_error = PLP_OK;
        if (interrupt_state == 0u)
        {
            __enable_irq();
        }
        return error;
    }
    if (uart->tx_busy != 0u ||
        uart->timer_state != NULINK2ME_TOOL0_TIMER_STATE_IDLE)
    {
        if (interrupt_state == 0u)
        {
            __enable_irq();
        }
        return PLP_ERR_SYSTEM_RESOURCE;
    }
    DisableReceiveStartInterrupt();
    uart->rx_enabled = 1u;
    uart->current_tx_value = value;
    uart->tx_frame = BuildTransmitFrame(value);
    uart->tx_bit_index = 0u;
    uart->tx_busy = 1u;
    uart->timer_state = NULINK2ME_TOOL0_TIMER_STATE_TX;
    DriveTransmitFrameBit(uart);
    StartTimer(NULINK2ME_TOOL0_TIMER_BIT_TICKS, TIMER_PERIODIC_MODE);
    if (interrupt_state == 0u)
    {
        __enable_irq();
    }
    return PLP_OK;
}

uint32_t Nulink2MeTool0SoftUart_TryReadByte(
    Nulink2MeTool0SoftUart *uart, uint8_t *value)
{
    uint32_t error;
    if (uart == 0 || value == 0 || uart->initialized == 0u ||
        uart->rx_enabled == 0u)
    {
        return PLP_ERR_DRIVER_PROFILE;
    }
    error = uart->rx_error;
    if (error != PLP_OK)
    {
        uart->rx_error = PLP_OK;
        return error;
    }
    if (uart->rx_head == uart->rx_tail)
    {
        return PLP_ERR_SYSTEM_RESOURCE;
    }
    *value = uart->rx_fifo[uart->rx_head &
                           (NULINK2ME_TOOL0_RX_FIFO_SIZE - 1u)];
    uart->rx_head++;
    return PLP_OK;
}

uint8_t Nulink2MeTool0SoftUart_IsTransmitIdle(
    const Nulink2MeTool0SoftUart *uart)
{
    if (uart == 0 || uart->initialized == 0u)
    {
        return 0u;
    }
    return uart->tx_busy == 0u ? 1u : 0u;
}

uint32_t Nulink2MeTool0SoftUart_GetFailureSnapshot(
    Nulink2MeTool0SoftUart *uart,
    Nulink2MeTool0FailureSnapshot *snapshot)
{
    uint32_t interrupt_state;
    if (uart == 0 || snapshot == 0 || uart->initialized == 0u)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    interrupt_state = __get_PRIMASK();
    __disable_irq();
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->valid = uart->failure_valid;
    snapshot->tx_value = uart->failure_tx_value;
    snapshot->tx_bit_index = uart->failure_tx_bit_index;
    snapshot->timer_state = uart->failure_timer_state;
    snapshot->pin_flags = uart->failure_pin_flags;
    snapshot->timer_count = uart->failure_timer_count;
    if (interrupt_state == 0u)
    {
        __enable_irq();
    }
    return PLP_OK;
}

void GPA_IRQHandler(void)
{
    Nulink2MeTool0SoftUart *uart;
    uart = g_tool0_uart;
    if (GPIO_GET_INT_FLAG(PA, BIT10) == 0u)
    {
        return;
    }
    GPIO_CLR_INT_FLAG(PA, BIT10);
    if (uart == 0 || uart->initialized == 0u || uart->rx_enabled == 0u)
    {
        return;
    }
    DisableReceiveStartInterrupt();
    if (ReadReceivePin() == 0u && uart->tx_busy != 0u &&
        uart->timer_state == NULINK2ME_TOOL0_TIMER_STATE_TX &&
        uart->tx_bit_index >= NULINK2ME_TOOL0_TX_FIRST_STOP_BIT)
    {
        /* PA.9 is already released; capture target turnaround without
         * waiting for the second-stop-bit timer interrupt. */
        StopTimer();
        NVIC_ClearPendingIRQ(TMR2_IRQn);
        uart->tx_busy = 0u;
        uart->timer_state = NULINK2ME_TOOL0_TIMER_STATE_RX_START;
        StartTimer(NULINK2ME_TOOL0_TIMER_HALF_BIT_TICKS,
                   TIMER_ONESHOT_MODE);
        return;
    }
    if (uart->tx_busy != 0u ||
        uart->timer_state != NULINK2ME_TOOL0_TIMER_STATE_IDLE ||
        ReadReceivePin() != 0u)
    {
        if (uart->tx_busy == 0u &&
            uart->timer_state == NULINK2ME_TOOL0_TIMER_STATE_IDLE)
        {
            EnableReceiveStartInterrupt(uart);
        }
        return;
    }
    uart->timer_state = NULINK2ME_TOOL0_TIMER_STATE_RX_START;
    StartTimer(NULINK2ME_TOOL0_TIMER_HALF_BIT_TICKS, TIMER_ONESHOT_MODE);
}

void TMR2_IRQHandler(void)
{
    Nulink2MeTool0SoftUart *uart;
    if (TIMER_GetIntFlag(TIMER2) == 0u)
    {
        return;
    }
    TIMER_ClearIntFlag(TIMER2);
    uart = g_tool0_uart;
    if (uart == 0 || uart->initialized == 0u)
    {
        StopTimer();
        return;
    }
    if (uart->timer_state == NULINK2ME_TOOL0_TIMER_STATE_TX)
    {
        ProcessTransmitTimer(uart);
    }
    else if (uart->timer_state == NULINK2ME_TOOL0_TIMER_STATE_RX_START)
    {
        ProcessReceiveStartTimer(uart);
    }
    else if (uart->timer_state == NULINK2ME_TOOL0_TIMER_STATE_RX_BITS)
    {
        ProcessReceiveBitTimer(uart);
    }
    else
    {
        StopTimer();
    }
}

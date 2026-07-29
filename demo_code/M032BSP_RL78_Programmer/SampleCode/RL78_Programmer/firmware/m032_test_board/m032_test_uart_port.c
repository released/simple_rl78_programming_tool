#include "m032_test_uart_port.h"

#include <string.h>

#include "m032_test_board_profile.h"
#include "rl78_programmer/plp_frame.h"

static M032TestUartPort *g_uart_port;

static uint32_t SuspendPortInterrupt(void)
{
    uint32_t was_enabled;
    was_enabled = NVIC_GetEnableIRQ(UART02_IRQn);
    NVIC_DisableIRQ(UART02_IRQn);
    __DSB();
    __ISB();
    return was_enabled;
}

static void RestorePortInterrupt(uint32_t was_enabled)
{
    if (was_enabled != 0u)
    {
        NVIC_EnableIRQ(UART02_IRQn);
    }
}

static uint8_t CanSend(void *context)
{
    M032TestUartPort *port;
    port = (M032TestUartPort *)context;
    return port != 0 && port->tx_busy == 0u ? 1u : 0u;
}

static void FillTransmitFifo(M032TestUartPort *port)
{
    while (port->tx_index < PLP_REPORT_SIZE &&
           UART_GET_TX_FULL(UART0) == 0u)
    {
        UART_WRITE(UART0, port->tx_report[port->tx_index]);
        port->tx_index++;
    }
    if (port->tx_index >= PLP_REPORT_SIZE)
    {
        UART0->INTEN &= ~UART_INTEN_THREIEN_Msk;
        port->tx_all_queued = 1u;
        UART0->INTSTS = UART_INTSTS_TXENDIF_Msk;
        UART0->INTEN |= UART_INTEN_TXENDIEN_Msk;
    }
}

static uint32_t Send(void *context,
                     const uint8_t report[PLP_REPORT_SIZE])
{
    M032TestUartPort *port;
    uint32_t interrupt_state;
    port = (M032TestUartPort *)context;
    if (port == 0 || report == 0)
    {
        return PLP_ERR_SYSTEM_RESOURCE;
    }
    interrupt_state = SuspendPortInterrupt();
    if (port->tx_busy != 0u)
    {
        RestorePortInterrupt(interrupt_state);
        return PLP_ERR_SYSTEM_RESOURCE;
    }
    memcpy(port->tx_report, report, PLP_REPORT_SIZE);
    port->tx_index = 0u;
    port->tx_busy = 1u;
    port->tx_all_queued = 0u;
    UART0->INTEN &= ~UART_INTEN_TXENDIEN_Msk;
    UART0->INTSTS = UART_INTSTS_TXENDIF_Msk;
    FillTransmitFifo(port);
    if (port->tx_busy != 0u)
    {
        UART0->INTEN |= UART_INTEN_THREIEN_Msk;
    }
    RestorePortInterrupt(interrupt_state);
    return PLP_OK;
}

void M032TestUartPort_GetOps(M032TestUartPort *port,
                             M032LinkPortOps *ops)
{
    if (port == 0 || ops == 0)
    {
        return;
    }
    memset(ops, 0, sizeof(*ops));
    ops->context = port;
    ops->can_send = CanSend;
    ops->send = Send;
}

static void ResynchronizeReceive(M032TestUartPort *port)
{
    uint8_t index;
    uint8_t remaining;
    for (index = 1u; index < port->rx_length; ++index)
    {
        if (port->rx_report[index] == PLP_MAGIC)
        {
            remaining = (uint8_t)(port->rx_length - index);
            memmove(port->rx_report, port->rx_report + index,
                    remaining);
            port->rx_length = remaining;
            return;
        }
    }
    port->rx_length = 0u;
}

static void ReceiveByte(M032TestUartPort *port, uint8_t value)
{
    RppPlpFrame frame;
    if (port->report_pending != 0u)
    {
        return;
    }
    if (port->rx_length == 0u && value != PLP_MAGIC)
    {
        return;
    }
    port->rx_report[port->rx_length] = value;
    port->rx_length++;
    if (port->rx_length < PLP_REPORT_SIZE)
    {
        return;
    }
    memset(&frame, 0, sizeof(frame));
    if (Rpp_PlpDecode(port->rx_report, &frame) == PLP_OK)
    {
        memcpy(port->pending_report, port->rx_report,
               PLP_REPORT_SIZE);
        port->report_pending = 1u;
        port->rx_length = 0u;
    }
    else
    {
        ResynchronizeReceive(port);
    }
}

uint32_t M032TestUartPort_Init(M032TestUartPort *port,
                               M032Application *application)
{
    if (port == 0 || application == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    memset(port, 0, sizeof(*port));
    port->application = application;
    SYS_UnlockReg();
    CLK_EnableModuleClock(UART0_MODULE);
    CLK_SetModuleClock(UART0_MODULE, CLK_CLKSEL1_UART0SEL_HIRC,
                       CLK_CLKDIV0_UART0(1));
    SYS_ResetModule(UART0_RST);
    SYS->GPF_MFPL =
        (SYS->GPF_MFPL & ~(SYS_GPF_MFPL_PF2MFP_Msk |
                           SYS_GPF_MFPL_PF3MFP_Msk)) |
        SYS_GPF_MFPL_PF2MFP_UART0_RXD |
        SYS_GPF_MFPL_PF3MFP_UART0_TXD;
    SYS_LockReg();
    UART_Open(UART0, M032_TEST_PC_UART_BAUD);
    UART_SetLine_Config(UART0, M032_TEST_PC_UART_BAUD,
                        UART_WORD_LEN_8, UART_PARITY_NONE,
                        UART_STOP_BIT_1);
    UART0->FIFO =
        (UART0->FIFO & ~UART_FIFO_RFITL_Msk) |
        UART_FIFO_RFITL_1BYTE;
    UART_EnableInt(UART0, UART_INTEN_RDAIEN_Msk |
                          UART_INTEN_RXTOIEN_Msk);
    g_uart_port = port;
    NVIC_ClearPendingIRQ(UART02_IRQn);
    NVIC_SetPriority(UART02_IRQn, M032_TEST_PC_UART_IRQ_PRIORITY);
    NVIC_EnableIRQ(UART02_IRQn);
    return PLP_OK;
}

void M032TestUartPort_Process(M032TestUartPort *port)
{
    uint8_t report[PLP_REPORT_SIZE];
    uint32_t interrupt_state;
    if (port == 0 || port->application == 0 ||
        port->report_pending == 0u)
    {
        return;
    }
    interrupt_state = SuspendPortInterrupt();
    if (port->report_pending == 0u)
    {
        RestorePortInterrupt(interrupt_state);
        return;
    }
    memcpy(report, port->pending_report, sizeof(report));
    port->report_pending = 0u;
    RestorePortInterrupt(interrupt_state);
    (void)M032Application_OnLinkReport(port->application, report,
                                       sizeof(report));
}

void UART02_IRQHandler(void)
{
    M032TestUartPort *port;
    uint32_t status;
    port = g_uart_port;
    if (port == 0)
    {
        return;
    }
    status = UART0->INTSTS;
    if ((status & (UART_INTSTS_RDAINT_Msk |
                   UART_INTSTS_RXTOINT_Msk)) != 0u)
    {
        while (UART_GET_RX_EMPTY(UART0) == 0u)
        {
            ReceiveByte(port, (uint8_t)UART_READ(UART0));
        }
    }
    if ((status & UART_INTSTS_THREINT_Msk) != 0u &&
        port->tx_busy != 0u)
    {
        FillTransmitFifo(port);
    }
    if ((status & UART_INTSTS_TXENDINT_Msk) != 0u &&
        port->tx_busy != 0u &&
        port->tx_all_queued != 0u)
    {
        UART0->INTEN &= ~UART_INTEN_TXENDIEN_Msk;
        UART0->INTSTS = UART_INTSTS_TXENDIF_Msk;
        port->tx_busy = 0u;
        port->tx_all_queued = 0u;
    }
}

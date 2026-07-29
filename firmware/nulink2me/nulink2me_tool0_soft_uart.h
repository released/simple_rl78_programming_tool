#ifndef NULINK2ME_TOOL0_SOFT_UART_H
#define NULINK2ME_TOOL0_SOFT_UART_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NULINK2ME_TOOL0_RX_FIFO_SIZE 8u

typedef struct Nulink2MeTool0FailureSnapshotTag
{
    uint8_t valid;
    uint8_t tx_value;
    uint8_t tx_bit_index;
    uint8_t timer_state;
    uint8_t pin_flags;
    uint16_t timer_count;
} Nulink2MeTool0FailureSnapshot;

typedef struct Nulink2MeTool0SoftUartTag
{
    volatile uint16_t tx_frame;
    volatile uint16_t failure_timer_count;
    volatile uint32_t rx_error;
    volatile uint8_t current_tx_value;
    volatile uint8_t tx_bit_index;
    volatile uint8_t tx_busy;
    volatile uint8_t timer_state;
    volatile uint8_t rx_enabled;
    volatile uint8_t rx_bit_index;
    volatile uint8_t rx_value;
    volatile uint8_t rx_parity;
    volatile uint8_t rx_frame_valid;
    volatile uint8_t rx_head;
    volatile uint8_t rx_tail;
    volatile uint8_t rx_fifo[NULINK2ME_TOOL0_RX_FIFO_SIZE];
    volatile uint8_t failure_valid;
    volatile uint8_t failure_tx_value;
    volatile uint8_t failure_tx_bit_index;
    volatile uint8_t failure_timer_state;
    volatile uint8_t failure_pin_flags;
    uint8_t initialized;
} Nulink2MeTool0SoftUart;

uint32_t Nulink2MeTool0SoftUart_Init(Nulink2MeTool0SoftUart *uart);
uint32_t Nulink2MeTool0SoftUart_SetTool0Low(
    Nulink2MeTool0SoftUart *uart, uint8_t drive_low);
uint32_t Nulink2MeTool0SoftUart_SetProgrammingMode(
    Nulink2MeTool0SoftUart *uart, uint8_t enabled);
uint32_t Nulink2MeTool0SoftUart_PrepareAttempt(
    Nulink2MeTool0SoftUart *uart);
uint32_t Nulink2MeTool0SoftUart_TryWriteByte(
    Nulink2MeTool0SoftUart *uart, uint8_t value);
uint32_t Nulink2MeTool0SoftUart_TryReadByte(
    Nulink2MeTool0SoftUart *uart, uint8_t *value);
uint8_t Nulink2MeTool0SoftUart_IsTransmitIdle(
    const Nulink2MeTool0SoftUart *uart);
uint32_t Nulink2MeTool0SoftUart_GetFailureSnapshot(
    Nulink2MeTool0SoftUart *uart,
    Nulink2MeTool0FailureSnapshot *snapshot);

#ifdef __cplusplus
}
#endif

#endif

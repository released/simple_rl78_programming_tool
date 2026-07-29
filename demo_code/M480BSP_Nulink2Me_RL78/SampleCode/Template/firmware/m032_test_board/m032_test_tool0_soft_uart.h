#ifndef M032_TEST_TOOL0_SOFT_UART_H
#define M032_TEST_TOOL0_SOFT_UART_H

#include <stdint.h>

#include "programmer_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define M032_TEST_TOOL0_RX_FIFO_SIZE 8u

typedef struct M032TestTool0SoftUartTag
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
    volatile uint8_t rx_fifo[M032_TEST_TOOL0_RX_FIFO_SIZE];
    volatile uint8_t failure_valid;
    volatile uint8_t failure_tx_value;
    volatile uint8_t failure_tx_bit_index;
    volatile uint8_t failure_timer_state;
    volatile uint8_t failure_pin_flags;
    uint8_t initialized;
} M032TestTool0SoftUart;

uint32_t M032TestTool0SoftUart_Init(M032TestTool0SoftUart *uart);
uint32_t M032TestTool0SoftUart_SetTool0Low(
    M032TestTool0SoftUart *uart, uint8_t drive_low);
uint32_t M032TestTool0SoftUart_SetProgrammingMode(
    M032TestTool0SoftUart *uart, uint8_t enabled);
uint32_t M032TestTool0SoftUart_PrepareAttempt(
    M032TestTool0SoftUart *uart);
uint32_t M032TestTool0SoftUart_TryWriteByte(
    M032TestTool0SoftUart *uart, uint8_t value);
uint32_t M032TestTool0SoftUart_TryReadByte(
    M032TestTool0SoftUart *uart, uint8_t *value);
uint8_t M032TestTool0SoftUart_IsTransmitIdle(
    const M032TestTool0SoftUart *uart);
uint32_t M032TestTool0SoftUart_GetFailureSnapshot(
    M032TestTool0SoftUart *uart,
    ProgrammerTargetFailureSnapshot *snapshot);

#ifdef __cplusplus
}
#endif

#endif

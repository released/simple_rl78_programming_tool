#ifndef M032_TEST_UART_PORT_H
#define M032_TEST_UART_PORT_H

#include <stdint.h>

#include "NuMicro.h"
#include "m032_application.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct M032TestUartPortTag
{
    M032Application *application;
    volatile uint8_t rx_length;
    volatile uint8_t report_pending;
    volatile uint8_t tx_index;
    volatile uint8_t tx_busy;
    uint8_t rx_report[PLP_REPORT_SIZE];
    uint8_t pending_report[PLP_REPORT_SIZE];
    uint8_t tx_report[PLP_REPORT_SIZE];
} M032TestUartPort;

uint32_t M032TestUartPort_Init(M032TestUartPort *port,
                               M032Application *application);
void M032TestUartPort_GetOps(M032TestUartPort *port,
                             M032LinkPortOps *ops);
void M032TestUartPort_Process(M032TestUartPort *port);

#ifdef __cplusplus
}
#endif

#endif

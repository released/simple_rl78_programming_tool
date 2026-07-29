#ifndef M032_BOARD_PROFILE_H
#define M032_BOARD_PROFILE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define M032_BOARD_PORT_A                  0u
#define M032_BOARD_PORT_B                  1u

#define M032_BOARD_TARGET_VDD_SENSE_ENABLED 0u

#define M032_BOARD_TOOL0_TX_PORT           M032_BOARD_PORT_A
#define M032_BOARD_TOOL0_TX_PIN            3u
#define M032_BOARD_UART_RX_PORT            M032_BOARD_PORT_A
#define M032_BOARD_UART_RX_PIN             2u
#define M032_BOARD_RESET_PORT              M032_BOARD_PORT_A
#define M032_BOARD_RESET_PIN               5u
#define M032_BOARD_VDD_ENABLE_PORT         M032_BOARD_PORT_A
#define M032_BOARD_VDD_ENABLE_PIN          4u
#define M032_BOARD_STATUS_LED_PORT          M032_BOARD_PORT_B
#define M032_BOARD_STATUS_LED_PIN           14u

#define M032_BOARD_VCOM_TX_PORT            M032_BOARD_PORT_B
#define M032_BOARD_VCOM_TX_PIN             13u
#define M032_BOARD_VCOM_RX_PORT            M032_BOARD_PORT_B
#define M032_BOARD_VCOM_RX_PIN             12u
#define M032_BOARD_TARGET_VDD_MIN_MV       2400u
#define M032_BOARD_TARGET_VDD_MAX_MV       5500u
#define M032_BOARD_TOOL0_SETUP_US          10u
#define M032_BOARD_TOOL0_HOLD_US           1000u
#define M032_BOARD_INITIAL_SETTING_US      100000u
#define M032_BOARD_UART_BAUD               115200u

#define M032_BOARD_PIN_ROLE_TOOL0_UART_TX  0u
#define M032_BOARD_PIN_ROLE_UART_RX        1u
#define M032_BOARD_PIN_ROLE_RESET          2u
#define M032_BOARD_PIN_ROLE_VDD_ENABLE     3u
#define M032_BOARD_PIN_ROLE_STATUS_LED     4u
#define M032_BOARD_PIN_COUNT               5u

typedef struct M032BoardPinAssignmentTag
{
    uint8_t role;
    uint8_t port;
    uint8_t pin;
    uint8_t mcu_pin;
    const char *signal;
    const char *connector;
    const char *arduino;
} M032BoardPinAssignment;

extern const M032BoardPinAssignment g_M032BoardPinAssignments[M032_BOARD_PIN_COUNT];

uint32_t M032BoardProfile_Validate(void);
uint8_t M032BoardProfile_IsReserved(uint8_t port, uint8_t pin);
uint8_t M032BoardProfile_IsStatusLed(uint8_t port, uint8_t pin);

#ifdef __cplusplus
}
#endif

#endif

#include "m032_board_profile.h"

#include "rl78_programmer/plp_contract.h"

const M032BoardPinAssignment g_M032BoardPinAssignments[M032_BOARD_PIN_COUNT] =
{
    { M032_BOARD_PIN_ROLE_TOOL0_UART_TX, M032_BOARD_TOOL0_TX_PORT,
      M032_BOARD_TOOL0_TX_PIN, 27u, "TOOL0/UART1_TXD", "JP5.21/JP5.22", "D10" },
    { M032_BOARD_PIN_ROLE_UART_RX, M032_BOARD_UART_RX_PORT,
      M032_BOARD_UART_RX_PIN, 28u, "UART1_RXD", "JP5.23/JP5.24", "D13" },
    { M032_BOARD_PIN_ROLE_RESET, M032_BOARD_RESET_PORT,
      M032_BOARD_RESET_PIN, 25u, "TARGET_nRESET", "JP5.17/JP5.18", "D8" },
    { M032_BOARD_PIN_ROLE_VDD_ENABLE, M032_BOARD_VDD_ENABLE_PORT,
      M032_BOARD_VDD_ENABLE_PIN, 26u, "TARGET_VDD_ENABLE", "JP5.19/JP5.20", "D9" },
    { M032_BOARD_PIN_ROLE_STATUS_LED, M032_BOARD_STATUS_LED_PORT,
      M032_BOARD_STATUS_LED_PIN, 54u, "RL78_UPGRADE_LED_n", "JP6.11/JP6.12", "-" }
};

uint8_t M032BoardProfile_IsReserved(uint8_t port, uint8_t pin)
{
    if (port != M032_BOARD_PORT_B)
    {
        return 0u;
    }
    if (pin == M032_BOARD_VCOM_TX_PIN || pin == M032_BOARD_VCOM_RX_PIN)
    {
        return 1u;
    }
    return 0u;
}

uint8_t M032BoardProfile_IsStatusLed(uint8_t port, uint8_t pin)
{
    return port == M032_BOARD_STATUS_LED_PORT &&
           pin == M032_BOARD_STATUS_LED_PIN ? 1u : 0u;
}

uint32_t M032BoardProfile_Validate(void)
{
    uint8_t left;
    uint8_t right;
    for (left = 0u; left < M032_BOARD_PIN_COUNT; ++left)
    {
        if (g_M032BoardPinAssignments[left].role != left ||
            M032BoardProfile_IsReserved(g_M032BoardPinAssignments[left].port,
                                        g_M032BoardPinAssignments[left].pin) != 0u)
        {
            return PLP_ERR_DRIVER_PROFILE;
        }
        for (right = (uint8_t)(left + 1u); right < M032_BOARD_PIN_COUNT; ++right)
        {
            if (g_M032BoardPinAssignments[left].port ==
                    g_M032BoardPinAssignments[right].port &&
                g_M032BoardPinAssignments[left].pin ==
                    g_M032BoardPinAssignments[right].pin)
            {
                return PLP_ERR_DRIVER_PROFILE;
            }
        }
    }
    return PLP_OK;
}

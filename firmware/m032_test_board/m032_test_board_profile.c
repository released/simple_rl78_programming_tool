#include "m032_test_board_profile.h"

#include "rl78_programmer/plp_contract.h"

typedef struct M032TestPinTag
{
    uint8_t port;
    uint8_t pin;
} M032TestPin;

uint32_t M032TestBoardProfile_Validate(void)
{
    static const M032TestPin pins[] =
    {
        {M032_TEST_TOOL0_TX_PORT, M032_TEST_TOOL0_TX_PIN},
        {M032_TEST_TOOL0_RX_PORT, M032_TEST_TOOL0_RX_PIN},
        {M032_TEST_RESET_PORT, M032_TEST_RESET_PIN},
        {M032_TEST_BUTTON_PORT, M032_TEST_BUTTON_PIN},
        {M032_TEST_LED_RED_PORT, M032_TEST_LED_RED_PIN},
        {M032_TEST_LED_GREEN_PORT, M032_TEST_LED_GREEN_PIN},
        {M032_TEST_PC_UART_TX_PORT, M032_TEST_PC_UART_TX_PIN},
        {M032_TEST_PC_UART_RX_PORT, M032_TEST_PC_UART_RX_PIN}
    };
    uint8_t left;
    uint8_t right;
    uint32_t boundary;
    uint32_t storage_size;
    boundary = M032_TEST_BOOT_BASE + M032_TEST_BOOT_SIZE;
    if (boundary != M032_TEST_APP_BASE)
    {
        return PLP_ERR_CONFIG_VALIDATION;
    }
    boundary = M032_TEST_APP_BASE + M032_TEST_APP_SIZE;
    if (boundary != M032_TEST_METADATA_BASE)
    {
        return PLP_ERR_CONFIG_VALIDATION;
    }
    boundary = M032_TEST_METADATA_BASE + M032_TEST_METADATA_SIZE;
    if (boundary != M032_TEST_SLOT_BASE)
    {
        return PLP_ERR_CONFIG_VALIDATION;
    }
    boundary = M032_TEST_SLOT_BASE + M032_TEST_SLOT_SIZE;
    storage_size = M032_TEST_STORAGE_SIZE;
    if (boundary != M032_TEST_APROM_SIZE ||
        storage_size != 0x00010400ul)
    {
        return PLP_ERR_CONFIG_VALIDATION;
    }
    for (left = 0u;
         left < (uint8_t)(sizeof(pins) / sizeof(pins[0]));
         ++left)
    {
        for (right = (uint8_t)(left + 1u);
             right < (uint8_t)(sizeof(pins) / sizeof(pins[0]));
             ++right)
        {
            if (pins[left].port == pins[right].port &&
                pins[left].pin == pins[right].pin)
            {
                return PLP_ERR_DRIVER_PROFILE;
            }
        }
    }
    return PLP_OK;
}

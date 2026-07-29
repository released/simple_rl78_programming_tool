#include "nulink2me_board_profile.h"

#include "rl78_programmer/plp_contract.h"

const Nulink2MePinAssignment
    g_Nulink2MePinAssignments[NULINK2ME_PIN_COUNT] =
{
    { NULINK2ME_ROLE_TOOL0_TX, NULINK2ME_PORT_A, 9u, 10u,
      "TOOL0_TX/GPIO_SOFT_UART", "SWDH_DAT/ICEJ4.3" },
    { NULINK2ME_ROLE_TOOL0_RX, NULINK2ME_PORT_A, 10u, 9u,
      "TOOL0_RX/GPIO_SOFT_UART", "SWDH_CLK/ICEJ4.5" },
    { NULINK2ME_ROLE_RESET, NULINK2ME_PORT_F, 5u, 14u,
      "TARGET_nRESET", "RESET/ICEJ4.7" },
    { NULINK2ME_ROLE_BUTTON, NULINK2ME_PORT_C, 7u, 18u,
      "OFFLINE_BUTTON_n", "KEY1" },
    { NULINK2ME_ROLE_ICE_LED, NULINK2ME_PORT_F, 4u, 15u,
      "ICE_LED_n", "Onboard" },
    { NULINK2ME_ROLE_ISP_LED, NULINK2ME_PORT_C, 6u, 19u,
      "ISP_LED_n", "Onboard" },
    { NULINK2ME_ROLE_RED_LED, NULINK2ME_PORT_B, 9u, 62u,
      "RED_LED_n", "Onboard" },
    { NULINK2ME_ROLE_GREEN_LED, NULINK2ME_PORT_B, 8u, 63u,
      "GREEN_LED_n", "Onboard" },
    { NULINK2ME_ROLE_SPIM_MOSI, NULINK2ME_PORT_C, 0u, 40u,
      "SPIM_MOSI0", "W25Q16.DI" },
    { NULINK2ME_ROLE_SPIM_MISO, NULINK2ME_PORT_C, 1u, 39u,
      "SPIM_MISO0", "W25Q16.DO" },
    { NULINK2ME_ROLE_SPIM_CLK, NULINK2ME_PORT_C, 2u, 38u,
      "SPIM_CLK", "W25Q16.CLK" },
    { NULINK2ME_ROLE_SPIM_SS, NULINK2ME_PORT_C, 3u, 37u,
      "SPIM_SS", "W25Q16.CS_n" },
    { NULINK2ME_ROLE_HXT_OUT, NULINK2ME_PORT_F, 2u, 17u,
      "HXT_O", "12MHz.XOUT" },
    { NULINK2ME_ROLE_HXT_IN, NULINK2ME_PORT_F, 3u, 16u,
      "HXT_I", "12MHz.XIN" }
};

uint32_t Nulink2MeBoardProfile_Validate(void)
{
    uint8_t left;
    uint8_t right;
    for (left = 0u; left < NULINK2ME_PIN_COUNT; ++left)
    {
        if (g_Nulink2MePinAssignments[left].role != left ||
            g_Nulink2MePinAssignments[left].signal == 0 ||
            g_Nulink2MePinAssignments[left].connector == 0)
        {
            return PLP_ERR_DRIVER_PROFILE;
        }
        for (right = (uint8_t)(left + 1u);
             right < NULINK2ME_PIN_COUNT; ++right)
        {
            if (g_Nulink2MePinAssignments[left].port ==
                    g_Nulink2MePinAssignments[right].port &&
                g_Nulink2MePinAssignments[left].pin ==
                    g_Nulink2MePinAssignments[right].pin)
            {
                return PLP_ERR_DRIVER_PROFILE;
            }
        }
    }
    return PLP_OK;
}

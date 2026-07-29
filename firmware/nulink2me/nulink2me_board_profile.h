#ifndef NULINK2ME_BOARD_PROFILE_H
#define NULINK2ME_BOARD_PROFILE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NULINK2ME_PORT_A 0u
#define NULINK2ME_PORT_B 1u
#define NULINK2ME_PORT_C 2u
#define NULINK2ME_PORT_F 5u

#define NULINK2ME_TARGET_VDD_SENSE_ENABLED 0u

#define NULINK2ME_ROLE_TOOL0_TX 0u
#define NULINK2ME_ROLE_TOOL0_RX 1u
#define NULINK2ME_ROLE_RESET 2u
#define NULINK2ME_ROLE_BUTTON 3u
#define NULINK2ME_ROLE_ICE_LED 4u
#define NULINK2ME_ROLE_ISP_LED 5u
#define NULINK2ME_ROLE_RED_LED 6u
#define NULINK2ME_ROLE_GREEN_LED 7u
#define NULINK2ME_ROLE_SPIM_MOSI 8u
#define NULINK2ME_ROLE_SPIM_MISO 9u
#define NULINK2ME_ROLE_SPIM_CLK 10u
#define NULINK2ME_ROLE_SPIM_SS 11u
#define NULINK2ME_ROLE_HXT_OUT 12u
#define NULINK2ME_ROLE_HXT_IN 13u
#define NULINK2ME_PIN_COUNT 14u

typedef struct Nulink2MePinAssignmentTag
{
    uint8_t role;
    uint8_t port;
    uint8_t pin;
    uint8_t package_pin;
    const char *signal;
    const char *connector;
} Nulink2MePinAssignment;

extern const Nulink2MePinAssignment
    g_Nulink2MePinAssignments[NULINK2ME_PIN_COUNT];

uint32_t Nulink2MeBoardProfile_Validate(void);

#ifdef __cplusplus
}
#endif

#endif

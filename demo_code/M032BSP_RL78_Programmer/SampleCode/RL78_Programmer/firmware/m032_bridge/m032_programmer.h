#ifndef M032_PROGRAMMER_H
#define M032_PROGRAMMER_H

#include <stdint.h>

#include "programmer_core.h"
#include "m032_status_led.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct M032ProgrammerIdentityTag
{
    const char *product_name;
    uint8_t programmer_uuid[16];
    uint8_t board_profile_uuid[16];
    uint8_t hardware_version[8];
    uint8_t firmware_version[8];
} M032ProgrammerIdentity;

typedef struct M032ProgrammerTag
{
    ProgrammerCore core;
    uint8_t out_report[PLP_REPORT_SIZE];
    uint8_t in_report[PLP_REPORT_SIZE];
    volatile uint8_t out_pending;
    volatile uint8_t in_pending;
    uint32_t offline_counter;
    M032StatusLed status_led;
    uint8_t status_led_attached;
    uint8_t autorun_pending;
    uint8_t autorun_suppressed;
    uint8_t autonomous_offline_active;
} M032Programmer;

void M032Programmer_Init(M032Programmer *programmer);
void M032Programmer_InitWithIdentity(
    M032Programmer *programmer,
    const M032ProgrammerIdentity *identity);
void M032Programmer_Configure(M032Programmer *programmer,
                              const ProgrammerStorageOps *storage,
                              const ProgrammerTargetOps *target,
                              uint32_t capabilities,
                              uint32_t (*uptime_ms)(void *context),
                              void *uptime_context);
uint32_t M032Programmer_ConfigureMode(M032Programmer *programmer,
                                     const ProgrammerModeOps *mode,
                                     uint8_t offline_reset_triggered);
uint32_t M032Programmer_OnOutReport(M032Programmer *programmer,
                                    const uint8_t *report, uint32_t length);
void M032Programmer_OnTransportDisconnected(M032Programmer *programmer);
void M032Programmer_Process(M032Programmer *programmer);
uint32_t M032Programmer_AttachBoardIo(M032Programmer *programmer,
                                      M032BoardIo *board);
uint32_t M032Programmer_UpdateIndicator(M032Programmer *programmer,
                                        uint32_t now_ms);
uint8_t M032Programmer_TakeInReport(M032Programmer *programmer,
                                    uint8_t report[PLP_REPORT_SIZE]);
uint32_t M032Programmer_OnOfflineButton(M032Programmer *programmer);
uint32_t M032Programmer_ProcessOfflineAutorun(M032Programmer *programmer,
                                              uint32_t now_ms,
                                              uint32_t grace_period_ms);

#ifdef __cplusplus
}
#endif

#endif

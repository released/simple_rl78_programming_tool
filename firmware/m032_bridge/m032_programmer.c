#include "m032_programmer.h"

#include <string.h>

#define M032_PROGRAMMER_CAPABILITIES \
    (PLP_CAP_REPLAY_CACHE | PLP_CAP_PAGED_QUERY | PLP_CAP_RECONNECT_QUERY)
#define M032_PROGRAM_STAGE_TARGET_CONNECT 4u

static const M032ProgrammerIdentity g_M032ProgrammerIdentity =
{
    "M032 RL78 Programmer",
    {
        0x4Du, 0x30u, 0x33u, 0x32u, 0x52u, 0x4Cu, 0x37u, 0x38u,
        0x80u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u
    },
    {
        0x52u, 0x4Cu, 0x37u, 0x38u, 0x47u, 0x31u, 0x55u, 0x00u,
        0x80u, 0x00u, 0x00u, 0x00u, 0x00u, 0x08u, 0x00u, 0x01u
    },
    {1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
    {1u, 0u, 1u, 0u, 2u, 0u, 0u, 0u}
};

static uint32_t M032Uptime(void *context)
{
    (void)context;
    return 0u;
}

void M032Programmer_Init(M032Programmer *programmer)
{
    M032Programmer_InitWithIdentity(programmer, &g_M032ProgrammerIdentity);
}

void M032Programmer_InitWithIdentity(
    M032Programmer *programmer,
    const M032ProgrammerIdentity *identity)
{
    ProgrammerCoreConfig config;
    const M032ProgrammerIdentity *selected_identity;
    if (programmer == 0)
    {
        return;
    }
    selected_identity = identity;
    if (selected_identity == 0)
    {
        selected_identity = &g_M032ProgrammerIdentity;
    }
    memset(programmer, 0, sizeof(*programmer));
    memset(&config, 0, sizeof(config));
    config.product_name = selected_identity->product_name;
    memcpy(config.programmer_uuid, selected_identity->programmer_uuid, 16u);
    memcpy(config.board_profile_uuid, selected_identity->board_profile_uuid, 16u);
    memcpy(config.hardware_version, selected_identity->hardware_version,
           sizeof(config.hardware_version));
    memcpy(config.firmware_version, selected_identity->firmware_version,
           sizeof(config.firmware_version));
    config.capabilities = M032_PROGRAMMER_CAPABILITIES;
    config.uptime_ms = M032Uptime;
    config.target.profile_ready = 0u;
    ProgrammerCore_Init(&programmer->core, &config);
}

void M032Programmer_Configure(M032Programmer *programmer,
                              const ProgrammerStorageOps *storage,
                              const ProgrammerTargetOps *target,
                              uint32_t capabilities,
                              uint32_t (*uptime_ms)(void *context),
                              void *uptime_context)
{
    if (programmer == 0)
    {
        return;
    }
    if (storage != 0)
    {
        memcpy(&programmer->core.config.storage, storage, sizeof(*storage));
    }
    if (target != 0)
    {
        memcpy(&programmer->core.config.target, target, sizeof(*target));
    }
    programmer->core.config.capabilities = capabilities | M032_PROGRAMMER_CAPABILITIES;
    programmer->core.config.uptime_ms = uptime_ms != 0 ? uptime_ms : M032Uptime;
    programmer->core.config.uptime_context = uptime_context;
}

uint32_t M032Programmer_ConfigureMode(M032Programmer *programmer,
                                     const ProgrammerModeOps *mode,
                                     uint8_t offline_reset_triggered)
{
    uint32_t status;
    if (programmer == 0 || mode == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    memcpy(&programmer->core.config.mode, mode, sizeof(*mode));
    status = ProgrammerCore_LoadOperatingMode(&programmer->core);
    programmer->autorun_pending =
        (status == PLP_OK &&
         programmer->core.operating_mode == PLP_OPERATING_MODE_OFFLINE_ARMED &&
         programmer->core.offline_autorun_enabled != 0u &&
         offline_reset_triggered != 0u) ? 1u : 0u;
    programmer->autorun_suppressed = 0u;
    return status;
}

uint32_t M032Programmer_OnOutReport(M032Programmer *programmer,
                                    const uint8_t *report, uint32_t length)
{
    if (programmer == 0 || report == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    if (length != PLP_REPORT_SIZE)
    {
        return PLP_ERR_BAD_LENGTH;
    }
    if (programmer->out_pending != 0u)
    {
        return PLP_ERR_SYSTEM_RESOURCE;
    }
    memcpy(programmer->out_report, report, PLP_REPORT_SIZE);
    programmer->out_pending = 1u;
    return PLP_OK;
}

void M032Programmer_Process(M032Programmer *programmer)
{
    uint32_t status;
    uint8_t disposition;
    if (programmer == 0)
    {
        return;
    }
    ProgrammerCore_Process(&programmer->core);
    if (programmer->autonomous_offline_active != 0u &&
        programmer->core.operation_state >= PLP_OPERATION_COMPLETED)
    {
        programmer->autonomous_offline_active = 0u;
    }
    if (programmer->core.session_open != 0u)
    {
        programmer->autorun_suppressed = 1u;
        programmer->autorun_pending = 0u;
        if (programmer->autonomous_offline_active != 0u &&
            programmer->core.active_source_kind == 1u &&
            programmer->core.operation_state == PLP_OPERATION_RUNNING &&
            programmer->core.job_stage == M032_PROGRAM_STAGE_TARGET_CONNECT &&
            programmer->core.config.target.cancel != 0)
        {
            disposition = 0u;
            status = programmer->core.config.target.cancel(
                programmer->core.config.target.context, &disposition);
            if (status == PLP_OK)
            {
                programmer->core.cancel_state = disposition;
                programmer->core.operation_state = PLP_OPERATION_CANCEL_PENDING;
            }
        }
    }
    if (programmer->out_pending != 0u && programmer->in_pending == 0u)
    {
        status = ProgrammerCore_HandleReport(&programmer->core,
                                              programmer->out_report,
                                              programmer->in_report);
        programmer->out_pending = 0u;
        if (status == PLP_OK)
        {
            programmer->in_pending = 1u;
        }
    }
}

void M032Programmer_OnTransportDisconnected(M032Programmer *programmer)
{
    if (programmer == 0)
    {
        return;
    }
    ProgrammerCore_OnTransportDisconnected(&programmer->core);
    programmer->out_pending = 0u;
    programmer->in_pending = 0u;
    programmer->autorun_suppressed = 0u;
}

uint32_t M032Programmer_AttachBoardIo(M032Programmer *programmer,
                                      M032BoardIo *board)
{
    uint32_t status;
    if (programmer == 0 || board == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    status = M032StatusLed_Init(&programmer->status_led, board);
    if (status == PLP_OK)
    {
        programmer->status_led_attached = 1u;
    }
    return status;
}

uint32_t M032Programmer_UpdateIndicator(M032Programmer *programmer,
                                        uint32_t now_ms)
{
    uint8_t mode;
    if (programmer == 0 || programmer->status_led_attached == 0u)
    {
        return PLP_ERR_DRIVER_PROFILE;
    }
    mode = M032_LED_MODE_READY;
    if (programmer->core.state == PLP_STATE_RECEIVING_IMAGE ||
        programmer->core.state == PLP_STATE_VALIDATING_IMAGE)
    {
        mode = M032_LED_MODE_TRANSFER;
    }
    else if (programmer->core.state == PLP_STATE_PROGRAMMING ||
             programmer->core.state == PLP_STATE_RECOVERING)
    {
        mode = M032_LED_MODE_PROGRAM;
    }
    else if (programmer->core.terminal_result == PLP_TERMINAL_PASS &&
             programmer->core.operation_state == PLP_OPERATION_COMPLETED)
    {
        mode = M032_LED_MODE_PASS;
    }
    else if (programmer->core.operation_state == PLP_OPERATION_FAILED ||
             programmer->core.operation_state == PLP_OPERATION_CANCELLED)
    {
        mode = M032_LED_MODE_FAIL;
    }
    M032StatusLed_SetMode(&programmer->status_led, mode);
    return M032StatusLed_Process(&programmer->status_led, now_ms);
}

uint8_t M032Programmer_TakeInReport(M032Programmer *programmer,
                                    uint8_t report[PLP_REPORT_SIZE])
{
    if (programmer == 0 || report == 0 || programmer->in_pending == 0u)
    {
        return 0u;
    }
    memcpy(report, programmer->in_report, PLP_REPORT_SIZE);
    programmer->in_pending = 0u;
    return 1u;
}

uint32_t M032Programmer_OnOfflineButton(M032Programmer *programmer)
{
    uint8_t job_uuid[16];
    uint32_t status;
    if (programmer == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    if (programmer->core.active_transfer_id != 0u)
    {
        return PLP_ERR_PROGRAM_BUSY;
    }
    if (programmer->core.operation_state == PLP_OPERATION_RUNNING)
    {
        if (programmer->autonomous_offline_active != 0u &&
            programmer->core.active_source_kind == 1u &&
            programmer->core.job_stage == M032_PROGRAM_STAGE_TARGET_CONNECT &&
            programmer->core.config.target.retry_connect != 0)
        {
            return programmer->core.config.target.retry_connect(
                programmer->core.config.target.context);
        }
        return PLP_ERR_PROGRAM_BUSY;
    }
    if (programmer->core.operation_state == PLP_OPERATION_CANCEL_PENDING)
    {
        return PLP_ERR_PROGRAM_BUSY;
    }
    ProgrammerCore_ReleaseSession(&programmer->core);
    memset(job_uuid, 0, sizeof(job_uuid));
    programmer->offline_counter++;
    job_uuid[0] = 0x4Fu;
    job_uuid[1] = 0x46u;
    job_uuid[2] = 0x46u;
    job_uuid[3] = 0x4Cu;
    job_uuid[4] = 0x49u;
    job_uuid[5] = 0x4Eu;
    job_uuid[6] = 0x45u;
    job_uuid[7] = 0x01u;
    job_uuid[12] = (uint8_t)programmer->offline_counter;
    job_uuid[13] = (uint8_t)(programmer->offline_counter >> 8);
    job_uuid[14] = (uint8_t)(programmer->offline_counter >> 16);
    job_uuid[15] = (uint8_t)(programmer->offline_counter >> 24);
    status = ProgrammerCore_StartOffline(&programmer->core, job_uuid);
    if (status == PLP_OK)
    {
        programmer->autonomous_offline_active = 1u;
    }
    return status;
}

uint32_t M032Programmer_ProcessOfflineAutorun(M032Programmer *programmer,
                                              uint32_t now_ms,
                                              uint32_t grace_period_ms)
{
    if (programmer == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    if (programmer->autorun_suppressed != 0u ||
        programmer->autorun_pending == 0u || now_ms < grace_period_ms)
    {
        return PLP_OK;
    }
    programmer->autorun_pending = 0u;
    return M032Programmer_OnOfflineButton(programmer);
}

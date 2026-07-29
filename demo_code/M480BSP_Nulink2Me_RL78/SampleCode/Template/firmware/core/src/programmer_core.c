#include "programmer_core.h"

#include <string.h>

#include "rl78_programmer/plp_frame.h"

#define DEVICE_INFO_BUFFER_SIZE 128u

static uint8_t UuidIsZero(const uint8_t uuid[16])
{
    uint8_t index;
    for (index = 0u; index < 16u; ++index)
    {
        if (uuid[index] != 0u)
        {
            return 0u;
        }
    }
    return 1u;
}

static uint32_t ReadUptime(const ProgrammerCore *core)
{
    if (core->config.uptime_ms == 0)
    {
        return 0u;
    }
    return core->config.uptime_ms(core->config.uptime_context);
}

static void UpdateJobElapsed(ProgrammerCore *core)
{
    if (core->config.uptime_ms != 0)
    {
        core->job_elapsed_ms = ReadUptime(core) - core->job_started_ms;
    }
}

static uint32_t ReadOfflineJobCounter(const uint8_t job_uuid[16])
{
    return (uint32_t)job_uuid[12] |
           ((uint32_t)job_uuid[13] << 8u) |
           ((uint32_t)job_uuid[14] << 16u) |
           ((uint32_t)job_uuid[15] << 24u);
}

static void StoreOfflineFailureHistory(ProgrammerCore *core)
{
    ProgrammerOfflineFailureRecord *record;
    uint32_t sequence;
    uint8_t index;
    if (core->last_offline_terminal_result == PLP_TERMINAL_PASS)
    {
        return;
    }
    sequence = core->offline_failure_sequence + 1u;
    if (sequence == 0u)
    {
        sequence = 1u;
    }
    core->offline_failure_sequence = sequence;
    if (core->offline_failure_count <
        PROGRAMMER_OFFLINE_FAILURE_HISTORY_CAPACITY)
    {
        index = (uint8_t)((core->offline_failure_head +
                           core->offline_failure_count) %
                          PROGRAMMER_OFFLINE_FAILURE_HISTORY_CAPACITY);
        core->offline_failure_count++;
    }
    else
    {
        index = core->offline_failure_head;
        core->offline_failure_head =
            (uint8_t)((core->offline_failure_head + 1u) %
                      PROGRAMMER_OFFLINE_FAILURE_HISTORY_CAPACITY);
        if (core->offline_failure_dropped != 0xFFFFFFFFu)
        {
            core->offline_failure_dropped++;
        }
    }
    record = &core->offline_failure_history[index];
    memset(record, 0, sizeof(*record));
    record->sequence = sequence;
    record->captured_uptime_ms = ReadUptime(core);
    record->job_counter = ReadOfflineJobCounter(core->last_offline_job_uuid);
    record->error = core->last_offline_error;
    record->attempt = core->last_offline_attempt;
    record->detail = core->last_offline_detail;
    record->terminal_result = core->last_offline_terminal_result;
    record->stage = core->last_offline_stage;
    memcpy(&record->snapshot, &core->last_offline_failure_snapshot,
           sizeof(record->snapshot));
}

static void StoreLastOfflineResult(ProgrammerCore *core)
{
    uint32_t status;
    if (core->active_source_kind != 1u)
    {
        return;
    }
    memcpy(core->last_offline_job_uuid, core->active_job_uuid, 16u);
    core->last_offline_attempt = core->attempt;
    core->last_offline_terminal_result = core->terminal_result;
    core->last_offline_stage = core->job_stage;
    core->last_offline_error = core->job_error;
    core->last_offline_detail = core->job_detail;
    core->last_offline_elapsed_ms = core->job_elapsed_ms;
    memset(&core->last_offline_failure_snapshot, 0,
           sizeof(core->last_offline_failure_snapshot));
    if (core->config.target.get_failure_snapshot != 0)
    {
        status = core->config.target.get_failure_snapshot(
            core->config.target.context,
            &core->last_offline_failure_snapshot);
        if (status != PLP_OK)
        {
            memset(&core->last_offline_failure_snapshot, 0,
                   sizeof(core->last_offline_failure_snapshot));
        }
    }
    core->last_offline_valid = 1u;
    StoreOfflineFailureHistory(core);
}

static uint32_t BuildResponse(ProgrammerCore *core, const RppPlpFrame *request,
                              uint32_t status, const uint8_t *data,
                              uint16_t data_length,
                              uint8_t response[PLP_REPORT_SIZE])
{
    uint32_t result;
    result = Rpp_PlpBuildResponse(request->command, request->sequence, core->state,
                                  status, status == PLP_OK ? PLP_RESPONSE_DATA_VALID : 0u,
                                  0u, data, data_length, response);
    if (result == PLP_OK)
    {
        core->last_command = request->command;
        core->last_sequence = request->sequence;
        memcpy(core->last_response, response, PLP_REPORT_SIZE);
        core->last_response_valid = 1u;
    }
    return result;
}

static uint16_t AppendTlv(uint8_t *buffer, uint16_t offset, uint16_t type,
                          const uint8_t *value, uint8_t length)
{
    Rpp_WriteLe16(buffer + offset, type);
    buffer[offset + 2u] = 0u;
    buffer[offset + 3u] = length;
    if (length != 0u)
    {
        memcpy(buffer + offset + 4u, value, length);
    }
    return (uint16_t)(offset + 4u + length);
}

static uint16_t BuildDeviceInfo(const ProgrammerCore *core,
                                uint8_t buffer[DEVICE_INFO_BUFFER_SIZE])
{
    uint16_t used;
    uint8_t health[4];
    uint8_t product_length;
    size_t product_size;
    const char *product;
    used = 0u;
    product = core->config.product_name;
    if (product == 0)
    {
        product = "M032 RL78 Programmer";
    }
    product_size = strlen(product);
    if (product_size > 32u)
    {
        product_size = 32u;
    }
    product_length = (uint8_t)product_size;
    used = AppendTlv(buffer, used, 0x0001u, core->config.programmer_uuid, 16u);
    used = AppendTlv(buffer, used, 0x0002u, (const uint8_t *)product, product_length);
    used = AppendTlv(buffer, used, 0x0003u, core->config.hardware_version, 4u);
    used = AppendTlv(buffer, used, 0x0004u, core->config.firmware_version, 8u);
    used = AppendTlv(buffer, used, 0x0008u, core->config.board_profile_uuid, 16u);
    memset(health, 0, sizeof(health));
    used = AppendTlv(buffer, used, 0x0009u, health, 4u);
    return used;
}

static uint32_t HandleNegotiate(ProgrammerCore *core, const RppPlpFrame *request,
                                uint8_t response[PLP_REPORT_SIZE])
{
    uint8_t data[32];
    if (request->payload_length < 28u || request->payload[0] > PLP_VERSION_MAJOR ||
        request->payload[2] < PLP_VERSION_MAJOR)
    {
        return BuildResponse(core, request, PLP_ERR_VERSION, 0, 0u, response);
    }
    memset(data, 0, sizeof(data));
    data[0] = PLP_VERSION_MAJOR;
    data[1] = PLP_VERSION_MINOR;
    data[2] = 1u;
    data[3] = 4u;
    data[4] = PLP_VERSION_MAJOR;
    data[5] = PLP_VERSION_MINOR;
    data[6] = PLP_VERSION_MAJOR;
    data[7] = PLP_VERSION_MINOR;
    Rpp_WriteLe32(data + 8u, core->config.capabilities);
    data[12] = 1u;
    data[13] = 4u;
    memcpy(data + 16u, core->config.programmer_uuid, 16u);
    core->negotiated = 1u;
    core->state = PLP_STATE_READY;
    return BuildResponse(core, request, PLP_OK, data, sizeof(data), response);
}

static uint32_t HandleOpenSession(ProgrammerCore *core, const RppPlpFrame *request,
                                  uint8_t response[PLP_REPORT_SIZE])
{
    uint8_t data[24];
    if (core->negotiated == 0u)
    {
        return BuildResponse(core, request, PLP_ERR_INVALID_STATE, 0, 0u, response);
    }
    if (request->payload_length < 36u)
    {
        return BuildResponse(core, request, PLP_ERR_BAD_LENGTH, 0, 0u, response);
    }
    memset(data, 0, sizeof(data));
    memcpy(core->session_uuid, request->payload, 16u);
    core->session_uuid[15] ^= 0xA5u;
    memcpy(data, core->session_uuid, 16u);
    data[16] = 1u;
    data[17] = 1u;
    core->session_open = 1u;
    core->state = PLP_STATE_SESSION;
    return BuildResponse(core, request, PLP_OK, data, sizeof(data), response);
}

static uint32_t HandleCloseSession(ProgrammerCore *core,
                                   const RppPlpFrame *request,
                                   uint8_t response[PLP_REPORT_SIZE])
{
    uint8_t closed_session[16];
    if (request->payload_length < 20u)
    {
        return BuildResponse(core, request, PLP_ERR_BAD_LENGTH,
                             0, 0u, response);
    }
    if (core->session_open == 0u ||
        memcmp(core->session_uuid, request->payload, 16u) != 0)
    {
        return BuildResponse(core, request, PLP_ERR_INVALID_STATE,
                             0, 0u, response);
    }
    memcpy(closed_session, core->session_uuid, sizeof(closed_session));
    ProgrammerCore_ReleaseSession(core);
    return BuildResponse(core, request, PLP_OK, closed_session,
                         sizeof(closed_session), response);
}

static uint32_t HandleDeviceInfo(ProgrammerCore *core, const RppPlpFrame *request,
                                 uint8_t response[PLP_REPORT_SIZE])
{
    uint8_t all[DEVICE_INFO_BUFFER_SIZE];
    uint8_t data[44];
    uint16_t total;
    uint32_t cursor;
    uint8_t maximum;
    uint8_t length;
    if (request->payload_length < 12u)
    {
        return BuildResponse(core, request, PLP_ERR_BAD_LENGTH, 0, 0u, response);
    }
    total = BuildDeviceInfo(core, all);
    cursor = Rpp_ReadLe32(request->payload + 4u);
    maximum = request->payload[8u];
    if (cursor > total)
    {
        return BuildResponse(core, request, PLP_ERR_CURSOR_STALE, 0, 0u, response);
    }
    length = (uint8_t)(total - (uint16_t)cursor);
    if (length > 35u)
    {
        length = 35u;
    }
    if (length > maximum)
    {
        length = maximum;
    }
    memset(data, 0, sizeof(data));
    Rpp_WriteLe32(data, 1u);
    Rpp_WriteLe32(data + 4u, (cursor + length < total) ? cursor + length : 0u);
    data[8] = length;
    if (length != 0u)
    {
        memcpy(data + 9u, all + cursor, length);
    }
    return BuildResponse(core, request, PLP_OK, data, (uint16_t)(9u + length), response);
}

static uint32_t HandleStatus(ProgrammerCore *core, const RppPlpFrame *request,
                             uint8_t response[PLP_REPORT_SIZE])
{
    uint8_t data[44];
    memset(data, 0, sizeof(data));
    data[0] = core->state;
    memcpy(data + 5u, core->session_uuid, 16u);
    memcpy(data + 21u, core->active_job_uuid, 16u);
    Rpp_WriteLe16(data + 37u, core->active_transfer_id);
    data[39] = UuidIsZero(core->active_package_uuid) != 0u ? 0xFFu : 0u;
    return BuildResponse(core, request, PLP_OK, data, sizeof(data), response);
}

static void BuildOperatingModeData(ProgrammerCore *core, uint8_t data[24])
{
    uint8_t active[16];
    uint32_t generation;
    uint32_t status;
    memset(data, 0, 24u);
    data[0] = core->operating_mode;
    data[1] = core->offline_autorun_enabled;
    generation = core->active_generation;
    memcpy(active, core->active_package_uuid, 16u);
    if (core->config.storage.get_active != 0)
    {
        status = core->config.storage.get_active(core->config.storage.context,
                                                  active, &generation);
        if (status == PLP_OK)
        {
            memcpy(core->active_package_uuid, active, 16u);
            core->active_generation = generation;
        }
    }
    if (UuidIsZero(active) == 0u)
    {
        data[2] = 1u;
    }
    Rpp_WriteLe32(data + 4u, core->mode_generation);
    memcpy(data + 8u, active, 16u);
}

static uint32_t HandleGetOperatingMode(ProgrammerCore *core,
                                       const RppPlpFrame *request,
                                       uint8_t response[PLP_REPORT_SIZE])
{
    uint8_t data[24];
    BuildOperatingModeData(core, data);
    return BuildResponse(core, request, PLP_OK, data, sizeof(data), response);
}

static uint32_t HandleGetLastOfflineResult(
    ProgrammerCore *core, const RppPlpFrame *request,
    uint8_t response[PLP_REPORT_SIZE])
{
    uint8_t data[44];
    const ProgrammerTargetFailureSnapshot *snapshot;
    if (request->payload_length != 0u)
    {
        return BuildResponse(core, request, PLP_ERR_BAD_LENGTH, 0, 0u,
                             response);
    }
    memset(data, 0, sizeof(data));
    if (core->last_offline_valid != 0u)
    {
        memcpy(data, core->last_offline_job_uuid, 16u);
        Rpp_WriteLe16(data + 16u, core->last_offline_attempt);
        data[18] = core->last_offline_terminal_result;
        data[19] = core->last_offline_stage;
        Rpp_WriteLe32(data + 20u, core->last_offline_error);
        Rpp_WriteLe16(data + 24u, core->last_offline_detail);
        Rpp_WriteLe32(data + 26u, core->last_offline_elapsed_ms);
        snapshot = &core->last_offline_failure_snapshot;
        data[30] = snapshot->version;
        data[31] = snapshot->valid;
        data[32] = snapshot->protocol_step;
        data[33] = snapshot->unit_byte_index;
        data[34] = snapshot->tx_value;
        data[35] = snapshot->tx_bit_index;
        data[36] = snapshot->timer_state;
        data[37] = snapshot->pin_flags;
        Rpp_WriteLe32(data + 38u, snapshot->address);
        Rpp_WriteLe16(data + 42u, snapshot->timer_count);
    }
    return BuildResponse(core, request, PLP_OK, data, sizeof(data), response);
}

static uint32_t HandleGetOfflineFailureHistory(
    ProgrammerCore *core, const RppPlpFrame *request,
    uint8_t response[PLP_REPORT_SIZE])
{
    uint8_t data[44];
    const ProgrammerOfflineFailureRecord *record;
    const ProgrammerTargetFailureSnapshot *snapshot;
    uint32_t requested_sequence;
    uint32_t next_sequence;
    uint16_t dropped;
    uint8_t logical_index;
    uint8_t ring_index;
    uint8_t found;
    record = 0;
    snapshot = 0;
    if (request->payload_length != 4u)
    {
        return BuildResponse(core, request, PLP_ERR_BAD_LENGTH, 0, 0u,
                             response);
    }
    memset(data, 0, sizeof(data));
    requested_sequence = Rpp_ReadLe32(request->payload);
    dropped = core->offline_failure_dropped > 0xFFFFu ?
              0xFFFFu : (uint16_t)core->offline_failure_dropped;
    Rpp_WriteLe16(data + 8u, dropped);
    data[11] = PLP_OFFLINE_FAILURE_HISTORY_VERSION;
    if (core->offline_failure_dropped != 0u)
    {
        data[10] |= PLP_OFFLINE_FAILURE_HISTORY_OVERFLOW;
    }
    found = 0u;
    logical_index = 0u;
    for (logical_index = 0u;
         logical_index < core->offline_failure_count;
         ++logical_index)
    {
        ring_index = (uint8_t)((core->offline_failure_head + logical_index) %
                               PROGRAMMER_OFFLINE_FAILURE_HISTORY_CAPACITY);
        record = &core->offline_failure_history[ring_index];
        if (requested_sequence == 0u ||
            record->sequence >= requested_sequence)
        {
            found = 1u;
            break;
        }
    }
    if (found != 0u)
    {
        data[10] |= PLP_OFFLINE_FAILURE_HISTORY_VALID;
        Rpp_WriteLe32(data, record->sequence);
        next_sequence = 0u;
        if ((uint8_t)(logical_index + 1u) < core->offline_failure_count)
        {
            ring_index =
                (uint8_t)((core->offline_failure_head + logical_index + 1u) %
                          PROGRAMMER_OFFLINE_FAILURE_HISTORY_CAPACITY);
            next_sequence = core->offline_failure_history[ring_index].sequence;
        }
        Rpp_WriteLe32(data + 4u, next_sequence);
        Rpp_WriteLe32(data + 12u, record->job_counter);
        Rpp_WriteLe32(data + 16u, record->captured_uptime_ms);
        Rpp_WriteLe16(data + 20u, record->attempt);
        data[22] = record->terminal_result;
        data[23] = record->stage;
        Rpp_WriteLe32(data + 24u, record->error);
        Rpp_WriteLe16(data + 28u, record->detail);
        snapshot = &record->snapshot;
        data[30] = snapshot->version;
        data[31] = snapshot->valid;
        data[32] = snapshot->protocol_step;
        data[33] = snapshot->unit_byte_index;
        data[34] = snapshot->tx_value;
        data[35] = snapshot->tx_bit_index;
        data[36] = snapshot->timer_state;
        data[37] = snapshot->pin_flags;
        Rpp_WriteLe32(data + 38u, snapshot->address);
        Rpp_WriteLe16(data + 42u, snapshot->timer_count);
    }
    return BuildResponse(core, request, PLP_OK, data, sizeof(data), response);
}

static uint32_t HandleSetOperatingMode(ProgrammerCore *core,
                                       const RppPlpFrame *request,
                                       uint8_t response[PLP_REPORT_SIZE])
{
    uint8_t data[24];
    uint8_t mode;
    uint8_t autorun;
    uint8_t active[16];
    uint32_t active_generation;
    uint32_t expected_generation;
    uint32_t new_generation;
    uint32_t status;
    if (request->payload_length != 8u)
    {
        return BuildResponse(core, request, PLP_ERR_BAD_LENGTH, 0, 0u, response);
    }
    if (core->state == PLP_STATE_PROGRAMMING ||
        core->operation_state < PLP_OPERATION_COMPLETED)
    {
        return BuildResponse(core, request, PLP_ERR_PROGRAM_BUSY, 0, 0u, response);
    }
    mode = request->payload[0];
    autorun = request->payload[1];
    if (mode > PLP_OPERATING_MODE_OFFLINE_ARMED || autorun > 1u ||
        (mode == PLP_OPERATING_MODE_ONLINE_ONLY && autorun != 0u))
    {
        return BuildResponse(core, request, PLP_ERR_CONFIG_VALIDATION, 0, 0u, response);
    }
    if (mode == PLP_OPERATING_MODE_OFFLINE_ARMED)
    {
        if ((core->config.capabilities & PLP_CAP_OFFLINE_PACKAGE) == 0u ||
            (core->config.capabilities & PLP_CAP_TARGET_PROGRAM) == 0u ||
            core->config.storage.get_active == 0 ||
            core->config.target.profile_ready == 0u ||
            core->config.target.start == 0)
        {
            return BuildResponse(core, request, PLP_ERR_CAPABILITY_UNSUPPORTED,
                                 0, 0u, response);
        }
        if (autorun != 0u &&
            (core->config.capabilities & PLP_CAP_OFFLINE_AUTORUN) == 0u)
        {
            return BuildResponse(core, request, PLP_ERR_CAPABILITY_UNSUPPORTED,
                                 0, 0u, response);
        }
        status = core->config.storage.get_active(core->config.storage.context,
                                                  active, &active_generation);
        if (status != PLP_OK || UuidIsZero(active) != 0u)
        {
            return BuildResponse(core, request, PLP_ERR_OFFLINE_NOT_ACTIVE,
                                 0, 0u, response);
        }
        memcpy(core->active_package_uuid, active, 16u);
        core->active_generation = active_generation;
    }
    if (core->config.mode.store == 0)
    {
        return BuildResponse(core, request, PLP_ERR_CONFIG_PERSIST, 0, 0u, response);
    }
    expected_generation = Rpp_ReadLe32(request->payload + 4u);
    new_generation = 0u;
    status = core->config.mode.store(core->config.mode.context, mode, autorun,
                                     expected_generation, &new_generation);
    if (status != PLP_OK)
    {
        return BuildResponse(core, request, status, 0, 0u, response);
    }
    core->operating_mode = mode;
    core->offline_autorun_enabled = autorun;
    core->mode_generation = new_generation;
    BuildOperatingModeData(core, data);
    return BuildResponse(core, request, PLP_OK, data, sizeof(data), response);
}

static uint32_t HandleEnterBootloader(ProgrammerCore *core,
                                      const RppPlpFrame *request,
                                      uint8_t response[PLP_REPORT_SIZE])
{
    uint32_t status;
    if (request->payload_length != 0u)
    {
        return BuildResponse(core, request, PLP_ERR_BAD_LENGTH, 0, 0u, response);
    }
    if (core->negotiated == 0u || core->session_open == 0u)
    {
        return BuildResponse(core, request, PLP_ERR_INVALID_STATE, 0, 0u, response);
    }
    if ((core->config.capabilities & PLP_CAP_PROGRAMMER_UPDATE) == 0u ||
        core->config.boot.prepare == 0)
    {
        return BuildResponse(core, request, PLP_ERR_CAPABILITY_UNSUPPORTED,
                             0, 0u, response);
    }
    if (core->active_transfer_id != 0u ||
        core->operation_state < PLP_OPERATION_COMPLETED)
    {
        return BuildResponse(core, request, PLP_ERR_PROGRAM_BUSY, 0, 0u, response);
    }
    if (core->state != PLP_STATE_SESSION)
    {
        return BuildResponse(core, request, PLP_ERR_INVALID_STATE, 0, 0u, response);
    }
    status = core->config.boot.prepare(core->config.boot.context);
    if (status != PLP_OK)
    {
        return BuildResponse(core, request, status, 0, 0u, response);
    }
    core->state = PLP_STATE_UPDATING;
    return BuildResponse(core, request, PLP_OK, 0, 0u, response);
}

static uint32_t HandleResetProgrammer(ProgrammerCore *core,
                                      const RppPlpFrame *request,
                                      uint8_t response[PLP_REPORT_SIZE])
{
    uint32_t status;
    if (request->payload_length != 0u)
    {
        return BuildResponse(core, request, PLP_ERR_BAD_LENGTH, 0, 0u, response);
    }
    if (core->negotiated == 0u || core->session_open == 0u)
    {
        return BuildResponse(core, request, PLP_ERR_INVALID_STATE, 0, 0u, response);
    }
    if ((core->config.capabilities & PLP_CAP_PROGRAMMER_RESET) == 0u ||
        core->config.boot.prepare_reset == 0)
    {
        return BuildResponse(core, request, PLP_ERR_CAPABILITY_UNSUPPORTED,
                             0, 0u, response);
    }
    if (core->active_transfer_id != 0u ||
        core->operation_state < PLP_OPERATION_COMPLETED)
    {
        return BuildResponse(core, request, PLP_ERR_PROGRAM_BUSY, 0, 0u, response);
    }
    if (core->state != PLP_STATE_SESSION)
    {
        return BuildResponse(core, request, PLP_ERR_INVALID_STATE, 0, 0u, response);
    }
    status = core->config.boot.prepare_reset(core->config.boot.context);
    if (status != PLP_OK)
    {
        return BuildResponse(core, request, status, 0, 0u, response);
    }
    return BuildResponse(core, request, PLP_OK, 0, 0u, response);
}

static uint32_t HandleImageBegin(ProgrammerCore *core, const RppPlpFrame *request,
                                 uint8_t response[PLP_REPORT_SIZE])
{
    uint8_t data[26];
    uint32_t status;
    uint16_t transfer_id;
    uint32_t accepted;
    if (request->payload_length < 48u)
    {
        return BuildResponse(core, request, PLP_ERR_BAD_LENGTH, 0, 0u, response);
    }
    if (core->config.storage.begin == 0)
    {
        return BuildResponse(core, request, PLP_ERR_STORAGE_CAPACITY, 0, 0u, response);
    }
    transfer_id = 0u;
    accepted = 0u;
    status = core->config.storage.begin(core->config.storage.context, request->payload,
                                        Rpp_ReadLe32(request->payload + 16u),
                                        Rpp_ReadLe32(request->payload + 20u),
                                        request->payload + 28u, &transfer_id, &accepted);
    if (status != PLP_OK)
    {
        return BuildResponse(core, request, status, 0, 0u, response);
    }
    memcpy(core->transfer_package_uuid, request->payload, 16u);
    core->transfer_size = Rpp_ReadLe32(request->payload + 16u);
    core->transfer_offset = accepted;
    core->active_transfer_id = transfer_id;
    core->state = PLP_STATE_RECEIVING_IMAGE;
    memset(data, 0, sizeof(data));
    Rpp_WriteLe16(data, transfer_id);
    Rpp_WriteLe32(data + 2u, accepted);
    data[6] = PLP_IMAGE_CHUNK_DATA_SIZE;
    data[7] = 1u;
    data[8] = 0u;
    return BuildResponse(core, request, PLP_OK, data, sizeof(data), response);
}

static uint32_t HandleImageChunk(ProgrammerCore *core, const RppPlpFrame *request,
                                 uint8_t response[PLP_REPORT_SIZE])
{
    uint8_t data[8];
    uint32_t status;
    uint32_t accepted;
    uint8_t length;
    if (request->payload_length < 7u)
    {
        return BuildResponse(core, request, PLP_ERR_BAD_LENGTH, 0, 0u, response);
    }
    length = request->payload[6];
    if (request->payload_length != (uint16_t)(7u + length) ||
        Rpp_ReadLe16(request->payload) != core->active_transfer_id)
    {
        return BuildResponse(core, request, PLP_ERR_STORAGE_OFFSET, 0, 0u, response);
    }
    if (core->config.storage.write == 0)
    {
        return BuildResponse(core, request, PLP_ERR_STORAGE_WRITE, 0, 0u, response);
    }
    accepted = core->transfer_offset;
    status = core->config.storage.write(core->config.storage.context,
                                        core->active_transfer_id,
                                        Rpp_ReadLe32(request->payload + 2u),
                                        request->payload + 7u, length, &accepted);
    if (status != PLP_OK)
    {
        return BuildResponse(core, request, status, 0, 0u, response);
    }
    core->transfer_offset = accepted;
    memset(data, 0, sizeof(data));
    Rpp_WriteLe16(data, core->active_transfer_id);
    Rpp_WriteLe32(data + 2u, accepted);
    return BuildResponse(core, request, PLP_OK, data, sizeof(data), response);
}

static uint32_t HandleImageFinalize(ProgrammerCore *core, const RppPlpFrame *request,
                                    uint8_t response[PLP_REPORT_SIZE])
{
    uint8_t data[28];
    uint32_t status;
    if (request->payload_length < 42u ||
        Rpp_ReadLe16(request->payload) != core->active_transfer_id)
    {
        return BuildResponse(core, request, PLP_ERR_BAD_LENGTH, 0, 0u, response);
    }
    if (core->config.storage.finalize == 0)
    {
        return BuildResponse(core, request, PLP_ERR_STORAGE_WRITE, 0, 0u, response);
    }
    core->state = PLP_STATE_VALIDATING_IMAGE;
    status = core->config.storage.finalize(core->config.storage.context,
                                           core->active_transfer_id,
                                           Rpp_ReadLe32(request->payload + 2u),
                                           request->payload + 6u,
                                           Rpp_ReadLe32(request->payload + 38u));
    if (status != PLP_OK)
    {
        core->state = PLP_STATE_SESSION;
        return BuildResponse(core, request, status, 0, 0u, response);
    }
    memset(data, 0, sizeof(data));
    memcpy(data, core->transfer_package_uuid, 16u);
    Rpp_WriteLe32(data + 16u, 1u);
    data[24] = 0u;
    core->active_transfer_id = 0u;
    core->state = PLP_STATE_SESSION;
    return BuildResponse(core, request, PLP_OK, data, sizeof(data), response);
}

static uint32_t HandleImageActivate(ProgrammerCore *core, const RppPlpFrame *request,
                                    uint8_t response[PLP_REPORT_SIZE])
{
    uint8_t data[24];
    uint32_t generation;
    uint32_t status;
    if (request->payload_length < 40u)
    {
        return BuildResponse(core, request, PLP_ERR_BAD_LENGTH, 0, 0u, response);
    }
    if (core->config.storage.activate == 0)
    {
        return BuildResponse(core, request, PLP_ERR_STORAGE_NO_IMAGE, 0, 0u, response);
    }
    generation = 0u;
    status = core->config.storage.activate(core->config.storage.context, request->payload,
                                            Rpp_ReadLe32(request->payload + 16u), &generation);
    if (status != PLP_OK)
    {
        return BuildResponse(core, request, status, 0, 0u, response);
    }
    memcpy(core->active_package_uuid, request->payload, 16u);
    core->active_generation = generation;
    memset(data, 0, sizeof(data));
    memcpy(data, request->payload, 16u);
    Rpp_WriteLe32(data + 16u, generation);
    return BuildResponse(core, request, PLP_OK, data, sizeof(data), response);
}

static uint32_t StartJob(ProgrammerCore *core, const uint8_t job_uuid[16],
                         uint8_t source_kind, const uint8_t source_uuid[16],
                         const uint8_t profile_uuid[16], uint16_t flags)
{
    ProgrammerImageOps image;
    uint8_t selected_profile[16];
    uint32_t status;
    if (core->operation_state < PLP_OPERATION_COMPLETED &&
        UuidIsZero(core->active_job_uuid) == 0u)
    {
        return PLP_ERR_PROGRAM_BUSY;
    }
    if (core->config.target.profile_ready == 0u || core->config.target.start == 0)
    {
        return PLP_ERR_DRIVER_PROFILE;
    }
    if (source_kind == 1u && UuidIsZero(core->active_package_uuid) != 0u)
    {
        return PLP_ERR_OFFLINE_NOT_ACTIVE;
    }
    if (core->config.storage.open_image == 0)
    {
        return PLP_ERR_STORAGE_NO_IMAGE;
    }
    memset(&image, 0, sizeof(image));
    status = core->config.storage.open_image(core->config.storage.context,
                                             source_kind, source_uuid, &image);
    if (status != PLP_OK)
    {
        return status;
    }
    if (UuidIsZero(profile_uuid) != 0u)
    {
        memcpy(selected_profile, image.target_profile_uuid, 16u);
    }
    else
    {
        memcpy(selected_profile, profile_uuid, 16u);
    }
    if (memcmp(image.target_profile_uuid, selected_profile, 16u) != 0)
    {
        return PLP_ERR_IMAGE_INCOMPATIBLE;
    }
    status = core->config.target.start(core->config.target.context, &image,
                                       source_kind, source_uuid, selected_profile,
                                       flags);
    if (status != PLP_OK)
    {
        return status;
    }
    memcpy(core->active_job_uuid, job_uuid, 16u);
    core->active_source_kind = source_kind;
    core->operation_state = PLP_OPERATION_RUNNING;
    core->terminal_result = PLP_TERMINAL_NONE;
    core->job_stage = 1u;
    core->cancel_state = 0u;
    core->progress_basis_points = 0u;
    core->attempt = 1u;
    core->job_current = 0u;
    core->job_total = 0u;
    core->job_elapsed_ms = 0u;
    core->job_started_ms = ReadUptime(core);
    core->job_error = PLP_OK;
    core->job_detail = 0u;
    core->state = PLP_STATE_PROGRAMMING;
    return PLP_OK;
}

static uint32_t HandleProgramStart(ProgrammerCore *core, const RppPlpFrame *request,
                                   uint8_t response[PLP_REPORT_SIZE])
{
    uint8_t data[24];
    uint32_t status;
    if (request->payload_length < 52u)
    {
        return BuildResponse(core, request, PLP_ERR_BAD_LENGTH, 0, 0u, response);
    }
    status = StartJob(core, request->payload, request->payload[16],
                      request->payload + 20u, request->payload + 36u,
                      Rpp_ReadLe16(request->payload + 18u));
    if (status != PLP_OK)
    {
        return BuildResponse(core, request, status, 0, 0u, response);
    }
    memset(data, 0, sizeof(data));
    memcpy(data, core->active_job_uuid, 16u);
    data[16] = core->operation_state;
    data[17] = core->job_stage;
    Rpp_WriteLe16(data + 18u, core->attempt);
    return BuildResponse(core, request, PLP_OK, data, sizeof(data), response);
}

void ProgrammerCore_Process(ProgrammerCore *core)
{
    ProgrammerTargetStatus target;
    uint32_t status;
    if (core == 0 ||
        (core->operation_state != PLP_OPERATION_RUNNING &&
         core->operation_state != PLP_OPERATION_CANCEL_PENDING) ||
        core->config.target.poll == 0)
    {
        return;
    }
    memset(&target, 0, sizeof(target));
    status = core->config.target.poll(core->config.target.context, &target);
    UpdateJobElapsed(core);
    if (status != PLP_OK)
    {
        core->operation_state = PLP_OPERATION_FAILED;
        core->terminal_result = PLP_TERMINAL_FAIL;
        core->job_error = status;
        core->state = core->session_open != 0u ? PLP_STATE_SESSION : PLP_STATE_READY;
        StoreLastOfflineResult(core);
        return;
    }
    core->job_stage = target.stage;
    core->progress_basis_points = target.progress_basis_points;
    core->job_current = target.current;
    core->job_total = target.total;
    core->job_error = target.error;
    core->job_detail = target.detail;
    if (target.attempt != 0u)
    {
        core->attempt = target.attempt;
    }
    if (target.terminal != 0u)
    {
        core->terminal_result = target.terminal_result;
        if (target.terminal_result == PLP_TERMINAL_PASS && target.error == PLP_OK)
        {
            core->operation_state = PLP_OPERATION_COMPLETED;
        }
        else if (target.terminal_result == PLP_TERMINAL_CANCELLED)
        {
            core->operation_state = PLP_OPERATION_CANCELLED;
        }
        else
        {
            core->operation_state = PLP_OPERATION_FAILED;
        }
        core->state = core->session_open != 0u ? PLP_STATE_SESSION : PLP_STATE_READY;
        StoreLastOfflineResult(core);
    }
}

void ProgrammerCore_ReleaseSession(ProgrammerCore *core)
{
    if (core == 0)
    {
        return;
    }
    core->session_open = 0u;
    memset(core->session_uuid, 0, sizeof(core->session_uuid));
    if (core->operation_state != PLP_OPERATION_RUNNING &&
        core->operation_state != PLP_OPERATION_CANCEL_PENDING)
    {
        core->state = core->negotiated != 0u ?
            PLP_STATE_READY : PLP_STATE_READY_UNNEGOTIATED;
    }
}

void ProgrammerCore_OnTransportDisconnected(ProgrammerCore *core)
{
    if (core == 0)
    {
        return;
    }
    ProgrammerCore_ReleaseSession(core);
    core->negotiated = 0u;
    core->last_response_valid = 0u;
    if (core->operation_state != PLP_OPERATION_RUNNING &&
        core->operation_state != PLP_OPERATION_CANCEL_PENDING)
    {
        core->state = PLP_STATE_READY_UNNEGOTIATED;
    }
}

static uint32_t HandleGetJob(ProgrammerCore *core, const RppPlpFrame *request,
                             uint8_t response[PLP_REPORT_SIZE])
{
    uint8_t data[42];
    if (request->payload_length < 16u ||
        memcmp(request->payload, core->active_job_uuid, 16u) != 0)
    {
        return BuildResponse(core, request, PLP_ERR_PROGRAM_JOB_CONFLICT, 0, 0u, response);
    }
    ProgrammerCore_Process(core);
    memset(data, 0, sizeof(data));
    memcpy(data, core->active_job_uuid, 16u);
    data[16] = core->operation_state;
    data[17] = core->job_stage;
    data[18] = core->terminal_result;
    data[19] = core->cancel_state;
    Rpp_WriteLe16(data + 20u, core->progress_basis_points);
    Rpp_WriteLe16(data + 22u, core->attempt);
    Rpp_WriteLe32(data + 24u, core->job_current);
    Rpp_WriteLe32(data + 28u, core->job_total);
    Rpp_WriteLe32(data + 32u, core->job_elapsed_ms);
    Rpp_WriteLe32(data + 36u, core->job_error);
    Rpp_WriteLe16(data + 40u, core->job_detail);
    return BuildResponse(core, request, PLP_OK, data, sizeof(data), response);
}

static uint32_t HandleCancel(ProgrammerCore *core, const RppPlpFrame *request,
                             uint8_t response[PLP_REPORT_SIZE])
{
    uint8_t data[20];
    uint8_t disposition;
    uint32_t status;
    if (request->payload_length < 36u ||
        memcmp(request->payload, core->active_job_uuid, 16u) != 0)
    {
        return BuildResponse(core, request, PLP_ERR_PROGRAM_JOB_CONFLICT, 0, 0u, response);
    }
    disposition = 2u;
    if (core->operation_state < PLP_OPERATION_COMPLETED)
    {
        if (core->config.target.cancel == 0)
        {
            return BuildResponse(core, request, PLP_ERR_COMMAND_UNSUPPORTED, 0, 0u, response);
        }
        status = core->config.target.cancel(core->config.target.context, &disposition);
        if (status != PLP_OK)
        {
            return BuildResponse(core, request, status, 0, 0u, response);
        }
        core->cancel_state = disposition;
        core->operation_state = PLP_OPERATION_CANCEL_PENDING;
    }
    memset(data, 0, sizeof(data));
    memcpy(data, core->active_job_uuid, 16u);
    data[16] = disposition;
    data[17] = core->job_stage;
    return BuildResponse(core, request, PLP_OK, data, sizeof(data), response);
}

static uint32_t HandleSignature(ProgrammerCore *core, const RppPlpFrame *request,
                                uint8_t response[PLP_REPORT_SIZE])
{
    uint8_t data[36];
    uint16_t raw_length;
    uint16_t flags;
    uint32_t status;
    if (request->payload_length < 20u)
    {
        return BuildResponse(core, request, PLP_ERR_BAD_LENGTH, 0, 0u, response);
    }
    if (core->config.target.profile_ready == 0u || core->config.target.read_signature == 0)
    {
        return BuildResponse(core, request, PLP_ERR_DRIVER_PROFILE, 0, 0u, response);
    }
    memset(data, 0, sizeof(data));
    raw_length = 0u;
    flags = 0u;
    status = core->config.target.read_signature(core->config.target.context,
                                                 request->payload, data, data + 16u,
                                                 &raw_length, &flags);
    if (status != PLP_OK)
    {
        return BuildResponse(core, request, status, 0, 0u, response);
    }
    Rpp_WriteLe16(data + 32u, raw_length);
    Rpp_WriteLe16(data + 34u, flags);
    return BuildResponse(core, request, PLP_OK, data, sizeof(data), response);
}

void ProgrammerCore_Init(ProgrammerCore *core, const ProgrammerCoreConfig *config)
{
    if (core == 0)
    {
        return;
    }
    memset(core, 0, sizeof(*core));
    if (config != 0)
    {
        memcpy(&core->config, config, sizeof(*config));
    }
    core->state = PLP_STATE_READY_UNNEGOTIATED;
    core->operation_state = PLP_OPERATION_COMPLETED;
    core->operating_mode = PLP_OPERATING_MODE_ONLINE_ONLY;
    core->offline_autorun_enabled = 0u;
    if (core->config.mode.load != 0)
    {
        (void)ProgrammerCore_LoadOperatingMode(core);
    }
}

uint32_t ProgrammerCore_LoadOperatingMode(ProgrammerCore *core)
{
    uint8_t mode;
    uint8_t autorun;
    uint32_t generation;
    uint32_t status;
    if (core == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    core->operating_mode = PLP_OPERATING_MODE_ONLINE_ONLY;
    core->offline_autorun_enabled = 0u;
    core->mode_generation = 0u;
    if (core->config.mode.load == 0)
    {
        return PLP_OK;
    }
    mode = PLP_OPERATING_MODE_ONLINE_ONLY;
    autorun = 0u;
    generation = 0u;
    status = core->config.mode.load(core->config.mode.context, &mode, &autorun,
                                    &generation);
    if (status != PLP_OK)
    {
        return status;
    }
    if (mode > PLP_OPERATING_MODE_OFFLINE_ARMED || autorun > 1u ||
        (mode == PLP_OPERATING_MODE_ONLINE_ONLY && autorun != 0u))
    {
        return PLP_ERR_CONFIG_SCHEMA;
    }
    core->operating_mode = mode;
    core->offline_autorun_enabled = autorun;
    core->mode_generation = generation;
    return PLP_OK;
}

uint32_t ProgrammerCore_StartOffline(ProgrammerCore *core,
                                     const uint8_t job_uuid[16])
{
    uint8_t active[16];
    uint8_t profile[16];
    uint32_t generation;
    uint32_t status;
    if (core == 0 || job_uuid == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    if (core->operating_mode != PLP_OPERATING_MODE_OFFLINE_ARMED)
    {
        return PLP_ERR_OFFLINE_NOT_ACTIVE;
    }
    if (core->config.storage.get_active == 0)
    {
        return PLP_ERR_OFFLINE_NOT_ACTIVE;
    }
    status = core->config.storage.get_active(core->config.storage.context,
                                              active, &generation);
    if (status != PLP_OK)
    {
        return status;
    }
    memcpy(core->active_package_uuid, active, 16u);
    core->active_generation = generation;
    memset(profile, 0, sizeof(profile));
    return StartJob(core, job_uuid, 1u, active, profile, 0u);
}

uint32_t ProgrammerCore_HandleReport(ProgrammerCore *core,
                                     const uint8_t request_report[PLP_REPORT_SIZE],
                                     uint8_t response[PLP_REPORT_SIZE])
{
    RppPlpFrame request;
    uint8_t data[8];
    uint32_t status;
    if (core == 0 || request_report == 0 || response == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    status = Rpp_PlpDecode(request_report, &request);
    if (status != PLP_OK)
    {
        return status;
    }
    if ((request.flags & (PLP_FLAG_RESPONSE | PLP_FLAG_EVENT)) != 0u)
    {
        return PLP_ERR_BAD_FLAGS;
    }
    if ((request.flags & PLP_FLAG_RETRY) != 0u && core->last_response_valid != 0u &&
        request.command == core->last_command && request.sequence == core->last_sequence)
    {
        memcpy(response, core->last_response, PLP_REPORT_SIZE);
        return PLP_OK;
    }
    switch (request.command)
    {
        case PLP_CMD_NEGOTIATE:
            return HandleNegotiate(core, &request, response);
        case PLP_CMD_PING:
            if (request.payload_length != 4u)
            {
                return BuildResponse(core, &request, PLP_ERR_BAD_LENGTH, 0, 0u, response);
            }
            memcpy(data, request.payload, 4u);
            Rpp_WriteLe32(data + 4u, core->config.uptime_ms != 0 ?
                          core->config.uptime_ms(core->config.uptime_context) : 0u);
            return BuildResponse(core, &request, PLP_OK, data, sizeof(data), response);
        case PLP_CMD_OPEN_SESSION:
            return HandleOpenSession(core, &request, response);
        case PLP_CMD_CLOSE_SESSION:
            return HandleCloseSession(core, &request, response);
        case PLP_CMD_RESET_PROGRAMMER:
            return HandleResetProgrammer(core, &request, response);
        case PLP_CMD_GET_DEVICE_INFO:
            return HandleDeviceInfo(core, &request, response);
        case PLP_CMD_GET_STATUS:
            return HandleStatus(core, &request, response);
        case PLP_CMD_GET_LAST_OFFLINE_RESULT:
            return HandleGetLastOfflineResult(core, &request, response);
        case PLP_CMD_GET_OPERATING_MODE:
            return HandleGetOperatingMode(core, &request, response);
        case PLP_CMD_GET_OFFLINE_FAILURE_HISTORY:
            return HandleGetOfflineFailureHistory(core, &request, response);
        case PLP_CMD_IMAGE_BEGIN:
            return HandleImageBegin(core, &request, response);
        case PLP_CMD_IMAGE_WRITE_CHUNK:
            return HandleImageChunk(core, &request, response);
        case PLP_CMD_IMAGE_FINALIZE:
            return HandleImageFinalize(core, &request, response);
        case PLP_CMD_IMAGE_ACTIVATE:
            return HandleImageActivate(core, &request, response);
        case PLP_CMD_PROGRAM_START:
            return HandleProgramStart(core, &request, response);
        case PLP_CMD_PROGRAM_GET_JOB:
            return HandleGetJob(core, &request, response);
        case PLP_CMD_PROGRAM_CANCEL:
            return HandleCancel(core, &request, response);
        case PLP_CMD_TARGET_READ_SIGNATURE:
            return HandleSignature(core, &request, response);
        case PLP_CMD_SET_OPERATING_MODE:
            return HandleSetOperatingMode(core, &request, response);
        case PLP_CMD_FW_ENTER_BOOTLOADER:
            return HandleEnterBootloader(core, &request, response);
        default:
            return BuildResponse(core, &request, PLP_ERR_COMMAND_UNSUPPORTED, 0, 0u, response);
    }
}

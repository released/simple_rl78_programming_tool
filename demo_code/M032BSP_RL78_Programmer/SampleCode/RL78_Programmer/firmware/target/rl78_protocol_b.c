#include "rl78_protocol_b.h"

#include <string.h>

#include "rl78_programmer/oip_format.h"
#include "rl78_programmer/plp_contract.h"

#define RL78_B_MODE_SINGLE_WIRE          0x3Au
#define RL78_B_WRITE_CODE                0x60u
#define RL78_B_ACK                       0x06u
#define RL78_B_ACK_DATA_FLASH            0x36u
#define RL78_B_VERIFY_ERROR              0x1Bu

#define RL78_B_STAGE_CONNECT             4u
#define RL78_B_STAGE_ERASE               7u
#define RL78_B_STAGE_PROGRAM             8u
#define RL78_B_STAGE_VERIFY              9u
#define RL78_B_STAGE_RESET               11u
#define RL78_B_STAGE_COMPLETE            14u

#define RL78_B_STEP_IDLE                 0u
#define RL78_B_STEP_ENTER                1u
#define RL78_B_STEP_MODE_SEND            2u
#define RL78_B_STEP_MODE_TX_IDLE         3u
#define RL78_B_STEP_MODE_ACK             4u
#define RL78_B_STEP_RETRY_WAIT           5u
#define RL78_B_STEP_COMMAND_SEND         6u
#define RL78_B_STEP_COMMAND_TX_IDLE      7u
#define RL78_B_STEP_COMMAND_ACK          8u
#define RL78_B_STEP_COMMAND_SIZE         9u
#define RL78_B_STEP_PERMISSION_SEND      10u
#define RL78_B_STEP_PERMISSION_TX_IDLE   11u
#define RL78_B_STEP_ERASE_STATUS         12u
#define RL78_B_STEP_LOAD_UNIT            13u
#define RL78_B_STEP_UNIT_SEND            14u
#define RL78_B_STEP_UNIT_TX_IDLE         15u
#define RL78_B_STEP_UNIT_ACK             16u
#define RL78_B_STEP_VERIFY_STATUS        17u
#define RL78_B_STEP_RESTART              18u
#define RL78_B_STEP_TERMINAL              19u

#define RL78_B_HANDSHAKE_TIMEOUT_MS       100u
#define RL78_B_RESPONSE_TIMEOUT_MS        20u
#define RL78_B_PROGRAM_TIMEOUT_MS         20u
#define RL78_B_RETRY_INTERVAL_MS          500u
#define RL78_B_ONLINE_HANDSHAKE_ATTEMPTS  3u

typedef struct Rl78ProtocolBProfileTag
{
    uint8_t uuid[16];
    uint32_t code_size;
    uint8_t size_code;
} Rl78ProtocolBProfile;

static const Rl78ProtocolBProfile g_profiles[] =
{
    {
        {0x52u, 0x4Cu, 0x37u, 0x38u, 0x47u, 0x31u, 0x55u, 0x00u,
         0x80u, 0x00u, 0x00u, 0x00u, 0x00u, 0x04u, 0x00u, 0x01u},
        4ul * 1024ul,
        0x0Fu
    },
    {
        {0x52u, 0x4Cu, 0x37u, 0x38u, 0x47u, 0x31u, 0x55u, 0x00u,
         0x80u, 0x00u, 0x00u, 0x00u, 0x00u, 0x08u, 0x00u, 0x01u},
        8ul * 1024ul,
        0x1Fu
    },
    {
        {0x52u, 0x4Cu, 0x37u, 0x38u, 0x47u, 0x31u, 0x56u, 0x00u,
         0x80u, 0x00u, 0x00u, 0x00u, 0x00u, 0x10u, 0x00u, 0x01u},
        16ul * 1024ul,
        0x3Fu
    },
    {
        {0x52u, 0x4Cu, 0x37u, 0x38u, 0x47u, 0x31u, 0x56u, 0x00u,
         0x80u, 0x00u, 0x00u, 0x00u, 0x00u, 0x20u, 0x00u, 0x01u},
        32ul * 1024ul,
        0x7Fu
    }
};

static uint32_t Now(const Rl78ProtocolB *driver)
{
    return driver->transport.now_ms(driver->transport.context);
}

static uint8_t DeadlineReached(uint32_t now_ms, uint32_t deadline_ms)
{
    return ((int32_t)(now_ms - deadline_ms) >= 0) ? 1u : 0u;
}

static const Rl78ProtocolBProfile *FindProfile(const uint8_t uuid[16])
{
    uint32_t index;
    for (index = 0u;
         index < (uint32_t)(sizeof(g_profiles) / sizeof(g_profiles[0]));
         ++index)
    {
        if (memcmp(g_profiles[index].uuid, uuid, 16u) == 0)
        {
            return &g_profiles[index];
        }
    }
    return 0;
}

uint32_t Rl78ProtocolB_GetProfileCodeSize(
    const uint8_t target_profile_uuid[16], uint32_t *code_size)
{
    const Rl78ProtocolBProfile *profile;
    if (target_profile_uuid == 0 || code_size == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    profile = FindProfile(target_profile_uuid);
    if (profile == 0)
    {
        return PLP_ERR_DRIVER_PROFILE;
    }
    *code_size = profile->code_size;
    return PLP_OK;
}

static uint32_t EraseTimeout(uint32_t bytes)
{
    return 208ul + (6ul * bytes);
}

static uint32_t VerifyTimeout(uint32_t bytes)
{
    return bytes + ((bytes + 9ul) / 10ul);
}

static uint16_t ProgramUnitDetail(const Rl78ProtocolB *driver)
{
    uint32_t unit_index;
    unit_index = driver->address / 4u;
    if (unit_index > 0xFFFFu)
    {
        return 0xFFFFu;
    }
    return (uint16_t)unit_index;
}

static uint32_t CalculateDenseImageCrc(
    const ProgrammerImageOps *image, uint32_t code_size,
    uint32_t *crc32)
{
    uint8_t buffer[64];
    RppCrc32Context crc;
    uint32_t address;
    uint32_t length;
    uint32_t status;
    if (image == 0 || image->read_target == 0 || crc32 == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    Rpp_Crc32Init(&crc);
    address = 0u;
    while (address < code_size)
    {
        length = code_size - address;
        if (length > sizeof(buffer))
        {
            length = sizeof(buffer);
        }
        status = image->read_target(image->context, address,
                                    buffer, length);
        if (status != PLP_OK)
        {
            return status;
        }
        Rpp_Crc32Update(&crc, buffer, length);
        address += length;
    }
    *crc32 = Rpp_Crc32Final(&crc);
    return PLP_OK;
}

static void SetTerminal(Rl78ProtocolB *driver, uint8_t result,
                        uint32_t error, uint16_t detail)
{
    driver->terminal = 1u;
    driver->terminal_result = result;
    driver->error = error;
    driver->detail = detail;
    driver->step = RL78_B_STEP_TERMINAL;
}

static void CaptureFailureSnapshot(Rl78ProtocolB *driver)
{
    ProgrammerTargetFailureSnapshot link;
    uint32_t status;
    memset(&driver->failure_snapshot, 0, sizeof(driver->failure_snapshot));
    driver->failure_snapshot.version =
        PLP_OFFLINE_FAILURE_SNAPSHOT_VERSION;
    driver->failure_snapshot.valid = 1u;
    driver->failure_snapshot.protocol_step = driver->step;
    driver->failure_snapshot.unit_byte_index = driver->unit_index;
    driver->failure_snapshot.address = driver->address;
    if (driver->destructive_started != 0u)
    {
        driver->failure_snapshot.pin_flags |=
            PLP_OFFLINE_FAILURE_DESTRUCTIVE;
    }
    if (driver->transport.get_failure_snapshot != 0)
    {
        memset(&link, 0, sizeof(link));
        status = driver->transport.get_failure_snapshot(
            driver->transport.context, &link);
        if (status == PLP_OK && link.valid != 0u)
        {
            driver->failure_snapshot.tx_value = link.tx_value;
            driver->failure_snapshot.tx_bit_index = link.tx_bit_index;
            driver->failure_snapshot.timer_state = link.timer_state;
            driver->failure_snapshot.pin_flags |=
                (uint8_t)(link.pin_flags |
                          PLP_OFFLINE_FAILURE_LINK_VALID);
            driver->failure_snapshot.timer_count = link.timer_count;
        }
    }
}

static void FinishFailure(Rl78ProtocolB *driver, uint32_t error,
                          uint16_t detail, uint8_t command_completed)
{
    uint32_t cleanup;
    uint8_t result;
    CaptureFailureSnapshot(driver);
    result = PLP_TERMINAL_FAIL;
    if (driver->destructive_started != 0u &&
        command_completed == 0u && error == PLP_ERR_TARGET_TIMEOUT)
    {
        result = PLP_TERMINAL_OUTCOME_UNKNOWN;
    }
    else
    {
        cleanup = driver->transport.restart_target(driver->transport.context);
        if (error == PLP_OK && cleanup != PLP_OK)
        {
            error = cleanup;
        }
    }
    SetTerminal(driver, result, error, detail);
}

static void ScheduleHandshakeRetry(Rl78ProtocolB *driver, uint16_t detail)
{
    (void)driver->transport.leave_safe(driver->transport.context);
    if (driver->attempt < 0xFFFFu)
    {
        driver->attempt++;
    }
    driver->detail = detail;
    driver->deadline_ms = Now(driver) + RL78_B_RETRY_INTERVAL_MS;
    driver->step = RL78_B_STEP_RETRY_WAIT;
    driver->stage = RL78_B_STAGE_CONNECT;
}

static void HandshakeFailure(Rl78ProtocolB *driver, uint32_t error,
                             uint16_t detail)
{
    if (driver->cancel_requested == 0u &&
        (driver->offline_retry != 0u ||
         driver->attempt < RL78_B_ONLINE_HANDSHAKE_ATTEMPTS))
    {
        ScheduleHandshakeRetry(driver, detail);
    }
    else
    {
        FinishFailure(driver, error, detail, 1u);
    }
}

static uint32_t DriverStart(void *context, const ProgrammerImageOps *image,
                            uint8_t source_kind,
                            const uint8_t source_uuid[16],
                            const uint8_t target_profile_uuid[16],
                            uint16_t flags)
{
    Rl78ProtocolB *driver;
    const Rl78ProtocolBProfile *profile;
    uint32_t status;
    (void)source_uuid;
    (void)flags;
    driver = (Rl78ProtocolB *)context;
    if (driver == 0 || image == 0 || image->read_target == 0 ||
        image->has_region == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    if (driver->step != RL78_B_STEP_IDLE &&
        driver->step != RL78_B_STEP_TERMINAL)
    {
        return PLP_ERR_PROGRAM_BUSY;
    }
    profile = FindProfile(target_profile_uuid);
    if (profile == 0)
    {
        return PLP_ERR_DRIVER_PROFILE;
    }
    if (memcmp(image->target_profile_uuid, profile->uuid, 16u) != 0)
    {
        return PLP_ERR_IMAGE_INCOMPATIBLE;
    }
    if (image->has_region(image->context, RPP_OIP_SECTION_DATA_FLASH) != 0u)
    {
        return PLP_ERR_IMAGE_INCOMPATIBLE;
    }
    status = CalculateDenseImageCrc(
        image, profile->code_size, &driver->expected_image_crc32);
    if (status != PLP_OK)
    {
        return status;
    }
    memcpy(&driver->image, image, sizeof(*image));
    memset(&driver->failure_snapshot, 0, sizeof(driver->failure_snapshot));
    driver->code_size = profile->code_size;
    driver->address = 0u;
    driver->deadline_ms = 0u;
    driver->error = PLP_OK;
    driver->detail = 0u;
    driver->attempt = 1u;
    driver->progress_basis_points = 0u;
    driver->unit_index = 0u;
    driver->stage = RL78_B_STAGE_CONNECT;
    driver->terminal = 0u;
    driver->terminal_result = PLP_TERMINAL_NONE;
    driver->offline_retry = source_kind == 1u ? 1u : 0u;
    driver->destructive_started = 0u;
    driver->cancel_requested = 0u;
    Rpp_Crc32Init(&driver->transmitted_image_crc32);
    driver->step = RL78_B_STEP_ENTER;
    return PLP_OK;
}

static void ProcessEnter(Rl78ProtocolB *driver)
{
    uint32_t status;
    if (driver->cancel_requested != 0u)
    {
        (void)driver->transport.leave_safe(driver->transport.context);
        SetTerminal(driver, PLP_TERMINAL_CANCELLED, PLP_OK, 0u);
        return;
    }
    if (driver->transport.prepare_attempt != 0)
    {
        status = driver->transport.prepare_attempt(
            driver->transport.context);
        if (status != PLP_OK)
        {
            HandshakeFailure(driver, status, 0u);
            return;
        }
    }
    status = driver->transport.enter_programming_mode(
        driver->transport.context);
    if (status != PLP_OK)
    {
        HandshakeFailure(driver, status, 0u);
        return;
    }
    driver->deadline_ms = Now(driver) + RL78_B_HANDSHAKE_TIMEOUT_MS;
    driver->step = RL78_B_STEP_MODE_SEND;
}

static void ProcessModeSend(Rl78ProtocolB *driver)
{
    uint32_t status;
    status = driver->transport.try_write_byte(driver->transport.context,
                                               RL78_B_MODE_SINGLE_WIRE);
    if (status == PLP_ERR_SYSTEM_RESOURCE)
    {
        if (DeadlineReached(Now(driver), driver->deadline_ms) != 0u)
        {
            HandshakeFailure(driver, PLP_ERR_TARGET_TIMEOUT, 0u);
        }
        return;
    }
    if (status != PLP_OK)
    {
        HandshakeFailure(driver, status, 0u);
        return;
    }
    driver->step = RL78_B_STEP_MODE_TX_IDLE;
}

static void ProcessModeTransmitIdle(Rl78ProtocolB *driver)
{
    if (driver->transport.is_transmit_idle(driver->transport.context) == 0u)
    {
        if (DeadlineReached(Now(driver), driver->deadline_ms) != 0u)
        {
            HandshakeFailure(driver, PLP_ERR_TARGET_TIMEOUT, 0u);
        }
        return;
    }
    driver->deadline_ms = Now(driver) + RL78_B_HANDSHAKE_TIMEOUT_MS;
    driver->step = RL78_B_STEP_MODE_ACK;
}

static void ProcessModeAck(Rl78ProtocolB *driver)
{
    uint8_t value;
    uint32_t status;
    status = driver->transport.try_read_byte(driver->transport.context, &value);
    if (status == PLP_ERR_SYSTEM_RESOURCE)
    {
        if (DeadlineReached(Now(driver), driver->deadline_ms) != 0u)
        {
            HandshakeFailure(driver, PLP_ERR_TARGET_TIMEOUT, 0u);
        }
        return;
    }
    if (status != PLP_OK)
    {
        HandshakeFailure(driver, status, 0u);
        return;
    }
    if (value != RL78_B_ACK && value != RL78_B_ACK_DATA_FLASH)
    {
        HandshakeFailure(driver, PLP_ERR_DRIVER_TARGET_STATUS,
                         (uint16_t)value);
        return;
    }
    if (driver->cancel_requested != 0u)
    {
        FinishFailure(driver, PLP_OK, 0u, 1u);
        driver->terminal_result = PLP_TERMINAL_CANCELLED;
        return;
    }
    driver->stage = RL78_B_STAGE_ERASE;
    driver->deadline_ms = Now(driver) + RL78_B_RESPONSE_TIMEOUT_MS;
    driver->step = RL78_B_STEP_COMMAND_SEND;
}

static void ProcessCommandSend(Rl78ProtocolB *driver)
{
    uint32_t status;
    status = driver->transport.try_write_byte(driver->transport.context,
                                               RL78_B_WRITE_CODE);
    if (status == PLP_ERR_SYSTEM_RESOURCE)
    {
        if (DeadlineReached(Now(driver), driver->deadline_ms) != 0u)
        {
            FinishFailure(driver, PLP_ERR_TARGET_TIMEOUT, 0u, 1u);
        }
        return;
    }
    if (status != PLP_OK)
    {
        FinishFailure(driver, status, 0u, 1u);
        return;
    }
    driver->step = RL78_B_STEP_COMMAND_TX_IDLE;
}

static void ProcessCommandTransmitIdle(Rl78ProtocolB *driver)
{
    if (driver->transport.is_transmit_idle(driver->transport.context) == 0u)
    {
        if (DeadlineReached(Now(driver), driver->deadline_ms) != 0u)
        {
            FinishFailure(driver, PLP_ERR_TARGET_TIMEOUT, 0u, 1u);
        }
        return;
    }
    driver->deadline_ms = Now(driver) + RL78_B_RESPONSE_TIMEOUT_MS;
    driver->step = RL78_B_STEP_COMMAND_ACK;
}

static void ProcessCommandAck(Rl78ProtocolB *driver)
{
    uint8_t value;
    uint32_t status;
    status = driver->transport.try_read_byte(driver->transport.context, &value);
    if (status == PLP_ERR_SYSTEM_RESOURCE)
    {
        if (DeadlineReached(Now(driver), driver->deadline_ms) != 0u)
        {
            FinishFailure(driver, PLP_ERR_TARGET_TIMEOUT, 0u, 1u);
        }
        return;
    }
    if (status != PLP_OK)
    {
        FinishFailure(driver, status, 0u, 1u);
        return;
    }
    if (value != RL78_B_ACK)
    {
        FinishFailure(driver, PLP_ERR_DRIVER_TARGET_STATUS,
                      (uint16_t)value, 1u);
        return;
    }
    driver->step = RL78_B_STEP_COMMAND_SIZE;
}

static void ProcessCommandSize(Rl78ProtocolB *driver)
{
    const Rl78ProtocolBProfile *profile;
    uint8_t value;
    uint32_t status;
    profile = FindProfile(driver->image.target_profile_uuid);
    if (profile == 0)
    {
        FinishFailure(driver, PLP_ERR_DRIVER_PROFILE, 0u, 1u);
        return;
    }
    status = driver->transport.try_read_byte(driver->transport.context, &value);
    if (status == PLP_ERR_SYSTEM_RESOURCE)
    {
        if (DeadlineReached(Now(driver), driver->deadline_ms) != 0u)
        {
            FinishFailure(driver, PLP_ERR_TARGET_TIMEOUT, 0u, 1u);
        }
        return;
    }
    if (status != PLP_OK)
    {
        FinishFailure(driver, status, 0u, 1u);
        return;
    }
    if (value != profile->size_code)
    {
        FinishFailure(driver, PLP_ERR_DRIVER_TARGET_MISMATCH,
                      (uint16_t)value, 1u);
        return;
    }
    driver->deadline_ms = Now(driver) + RL78_B_RESPONSE_TIMEOUT_MS;
    driver->step = RL78_B_STEP_PERMISSION_SEND;
}

static void ProcessPermissionSend(Rl78ProtocolB *driver)
{
    uint32_t status;
    status = driver->transport.try_write_byte(driver->transport.context,
                                               RL78_B_ACK);
    if (status == PLP_ERR_SYSTEM_RESOURCE)
    {
        if (DeadlineReached(Now(driver), driver->deadline_ms) != 0u)
        {
            FinishFailure(driver, PLP_ERR_TARGET_TIMEOUT, 0u, 1u);
        }
        return;
    }
    if (status != PLP_OK)
    {
        FinishFailure(driver, status, 0u, 1u);
        return;
    }
    driver->destructive_started = 1u;
    driver->deadline_ms = Now(driver) + EraseTimeout(driver->code_size);
    driver->step = RL78_B_STEP_PERMISSION_TX_IDLE;
}

static void ProcessPermissionTransmitIdle(Rl78ProtocolB *driver)
{
    if (driver->transport.is_transmit_idle(driver->transport.context) == 0u)
    {
        if (DeadlineReached(Now(driver), driver->deadline_ms) != 0u)
        {
            FinishFailure(driver, PLP_ERR_TARGET_TIMEOUT,
                          ProgramUnitDetail(driver), 0u);
        }
        return;
    }
    driver->step = RL78_B_STEP_ERASE_STATUS;
}

static void ProcessEraseStatus(Rl78ProtocolB *driver)
{
    uint8_t value;
    uint32_t status;
    status = driver->transport.try_read_byte(driver->transport.context, &value);
    if (status == PLP_ERR_SYSTEM_RESOURCE)
    {
        if (DeadlineReached(Now(driver), driver->deadline_ms) != 0u)
        {
            FinishFailure(driver, PLP_ERR_TARGET_TIMEOUT, 0u, 0u);
        }
        return;
    }
    if (status != PLP_OK)
    {
        FinishFailure(driver, status, ProgramUnitDetail(driver), 0u);
        return;
    }
    if (value != RL78_B_ACK)
    {
        FinishFailure(driver, PLP_ERR_DRIVER_TARGET_STATUS,
                      (uint16_t)value, 1u);
        return;
    }
    driver->stage = RL78_B_STAGE_PROGRAM;
    driver->address = 0u;
    driver->step = RL78_B_STEP_LOAD_UNIT;
}

static void ProcessLoadUnit(Rl78ProtocolB *driver)
{
    uint32_t status;
    if (driver->address >= driver->code_size)
    {
        uint32_t transmitted_crc32;
        transmitted_crc32 =
            Rpp_Crc32Final(&driver->transmitted_image_crc32);
        if (transmitted_crc32 != driver->expected_image_crc32)
        {
            FinishFailure(driver, PLP_ERR_INTEGRITY_CRC, 0u, 1u);
            return;
        }
        driver->stage = RL78_B_STAGE_VERIFY;
        driver->deadline_ms = Now(driver) + VerifyTimeout(driver->code_size);
        driver->step = RL78_B_STEP_VERIFY_STATUS;
        return;
    }
    status = driver->image.read_target(driver->image.context, driver->address,
                                        driver->unit, 4u);
    if (status != PLP_OK)
    {
        FinishFailure(driver, status, 0u, 1u);
        return;
    }
    Rpp_Crc32Update(&driver->transmitted_image_crc32,
                    driver->unit, sizeof(driver->unit));
    driver->unit_index = 0u;
    driver->deadline_ms = Now(driver) + RL78_B_PROGRAM_TIMEOUT_MS;
    driver->step = RL78_B_STEP_UNIT_SEND;
}

static void ProcessUnitSend(Rl78ProtocolB *driver)
{
    uint32_t status;
    status = driver->transport.try_write_byte(
        driver->transport.context, driver->unit[driver->unit_index]);
    if (status == PLP_ERR_SYSTEM_RESOURCE)
    {
        if (DeadlineReached(Now(driver), driver->deadline_ms) != 0u)
        {
            FinishFailure(driver, PLP_ERR_TARGET_TIMEOUT,
                          ProgramUnitDetail(driver), 0u);
        }
        return;
    }
    if (status != PLP_OK)
    {
        FinishFailure(driver, status, ProgramUnitDetail(driver), 0u);
        return;
    }
    driver->unit_index++;
    if (driver->unit_index >= 4u)
    {
        driver->step = RL78_B_STEP_UNIT_TX_IDLE;
    }
}

static void ProcessUnitTransmitIdle(Rl78ProtocolB *driver)
{
    if (driver->transport.is_transmit_idle(driver->transport.context) == 0u)
    {
        if (DeadlineReached(Now(driver), driver->deadline_ms) != 0u)
        {
            FinishFailure(driver, PLP_ERR_TARGET_TIMEOUT,
                          ProgramUnitDetail(driver), 0u);
        }
        return;
    }
    driver->deadline_ms = Now(driver) + RL78_B_PROGRAM_TIMEOUT_MS;
    driver->step = RL78_B_STEP_UNIT_ACK;
}

static void ProcessUnitAck(Rl78ProtocolB *driver)
{
    uint8_t value;
    uint32_t status;
    status = driver->transport.try_read_byte(driver->transport.context, &value);
    if (status == PLP_ERR_SYSTEM_RESOURCE)
    {
        if (DeadlineReached(Now(driver), driver->deadline_ms) != 0u)
        {
            FinishFailure(driver, PLP_ERR_TARGET_TIMEOUT,
                          ProgramUnitDetail(driver), 0u);
        }
        return;
    }
    if (status != PLP_OK)
    {
        FinishFailure(driver, status, ProgramUnitDetail(driver), 0u);
        return;
    }
    if (value != RL78_B_ACK)
    {
        FinishFailure(driver, PLP_ERR_DRIVER_TARGET_STATUS,
                      (uint16_t)value, 1u);
        return;
    }
    driver->address += 4u;
    driver->progress_basis_points =
        (uint16_t)((driver->address * 10000ul) / driver->code_size);
    driver->step = RL78_B_STEP_LOAD_UNIT;
}

static void ProcessVerifyStatus(Rl78ProtocolB *driver)
{
    uint8_t value;
    uint32_t status;
    status = driver->transport.try_read_byte(driver->transport.context, &value);
    if (status == PLP_ERR_SYSTEM_RESOURCE)
    {
        if (DeadlineReached(Now(driver), driver->deadline_ms) != 0u)
        {
            FinishFailure(driver, PLP_ERR_TARGET_TIMEOUT, 0u, 0u);
        }
        return;
    }
    if (status != PLP_OK)
    {
        FinishFailure(driver, status, 0u, 0u);
        return;
    }
    if (value != RL78_B_ACK)
    {
        status = value == RL78_B_VERIFY_ERROR ?
                 PLP_ERR_VERIFY_MISMATCH : PLP_ERR_DRIVER_TARGET_STATUS;
        FinishFailure(driver, status, (uint16_t)value, 1u);
        return;
    }
    driver->stage = RL78_B_STAGE_RESET;
    driver->step = RL78_B_STEP_RESTART;
}

static void ProcessRestart(Rl78ProtocolB *driver)
{
    uint32_t status;
    status = driver->transport.restart_target(driver->transport.context);
    if (status != PLP_OK)
    {
        SetTerminal(driver, PLP_TERMINAL_FAIL, status, 0u);
        return;
    }
    driver->progress_basis_points = 10000u;
    driver->stage = RL78_B_STAGE_COMPLETE;
    if (driver->cancel_requested != 0u)
    {
        SetTerminal(driver, PLP_TERMINAL_CANCELLED, PLP_OK, 0u);
    }
    else
    {
        SetTerminal(driver, PLP_TERMINAL_PASS, PLP_OK, 0u);
    }
}

static uint32_t DriverPoll(void *context, ProgrammerTargetStatus *status)
{
    Rl78ProtocolB *driver;
    driver = (Rl78ProtocolB *)context;
    if (driver == 0 || status == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    switch (driver->step)
    {
        case RL78_B_STEP_ENTER:
            ProcessEnter(driver);
            break;
        case RL78_B_STEP_MODE_SEND:
            ProcessModeSend(driver);
            break;
        case RL78_B_STEP_MODE_TX_IDLE:
            ProcessModeTransmitIdle(driver);
            break;
        case RL78_B_STEP_MODE_ACK:
            ProcessModeAck(driver);
            break;
        case RL78_B_STEP_RETRY_WAIT:
            if (driver->cancel_requested != 0u)
            {
                (void)driver->transport.leave_safe(driver->transport.context);
                SetTerminal(driver, PLP_TERMINAL_CANCELLED, PLP_OK, 0u);
            }
            else if (DeadlineReached(Now(driver), driver->deadline_ms) != 0u)
            {
                driver->step = RL78_B_STEP_ENTER;
            }
            break;
        case RL78_B_STEP_COMMAND_SEND:
            ProcessCommandSend(driver);
            break;
        case RL78_B_STEP_COMMAND_TX_IDLE:
            ProcessCommandTransmitIdle(driver);
            break;
        case RL78_B_STEP_COMMAND_ACK:
            ProcessCommandAck(driver);
            break;
        case RL78_B_STEP_COMMAND_SIZE:
            ProcessCommandSize(driver);
            break;
        case RL78_B_STEP_PERMISSION_SEND:
            ProcessPermissionSend(driver);
            break;
        case RL78_B_STEP_PERMISSION_TX_IDLE:
            ProcessPermissionTransmitIdle(driver);
            break;
        case RL78_B_STEP_ERASE_STATUS:
            ProcessEraseStatus(driver);
            break;
        case RL78_B_STEP_LOAD_UNIT:
            ProcessLoadUnit(driver);
            break;
        case RL78_B_STEP_UNIT_SEND:
            ProcessUnitSend(driver);
            break;
        case RL78_B_STEP_UNIT_TX_IDLE:
            ProcessUnitTransmitIdle(driver);
            break;
        case RL78_B_STEP_UNIT_ACK:
            ProcessUnitAck(driver);
            break;
        case RL78_B_STEP_VERIFY_STATUS:
            ProcessVerifyStatus(driver);
            break;
        case RL78_B_STEP_RESTART:
            ProcessRestart(driver);
            break;
        case RL78_B_STEP_IDLE:
        case RL78_B_STEP_TERMINAL:
        default:
            break;
    }
    memset(status, 0, sizeof(*status));
    status->stage = driver->stage;
    status->terminal_result = driver->terminal_result;
    status->progress_basis_points = driver->progress_basis_points;
    status->current = driver->address;
    status->total = driver->code_size;
    status->error = driver->error;
    status->detail = driver->detail;
    status->attempt = driver->attempt;
    status->terminal = driver->terminal;
    return PLP_OK;
}

static uint32_t DriverCancel(void *context, uint8_t *disposition)
{
    Rl78ProtocolB *driver;
    driver = (Rl78ProtocolB *)context;
    if (driver == 0 || disposition == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    if (driver->terminal != 0u || driver->step == RL78_B_STEP_IDLE)
    {
        *disposition = 2u;
        return PLP_OK;
    }
    driver->cancel_requested = 1u;
    *disposition = driver->destructive_started != 0u ? 1u : 0u;
    return PLP_OK;
}

static uint32_t DriverRetryConnect(void *context)
{
    Rl78ProtocolB *driver;
    uint32_t status;
    driver = (Rl78ProtocolB *)context;
    if (driver == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    if (driver->offline_retry == 0u || driver->terminal != 0u ||
        driver->destructive_started != 0u ||
        driver->stage != RL78_B_STAGE_CONNECT)
    {
        return PLP_ERR_PROGRAM_BUSY;
    }
    status = driver->transport.leave_safe(driver->transport.context);
    if (status != PLP_OK)
    {
        return status;
    }
    if (driver->step != RL78_B_STEP_RETRY_WAIT &&
        driver->attempt < 0xFFFFu)
    {
        driver->attempt++;
    }
    driver->cancel_requested = 0u;
    driver->deadline_ms = 0u;
    driver->error = PLP_OK;
    driver->detail = 0u;
    driver->step = RL78_B_STEP_ENTER;
    driver->stage = RL78_B_STAGE_CONNECT;
    return PLP_OK;
}

static uint32_t DriverGetFailureSnapshot(
    void *context, ProgrammerTargetFailureSnapshot *snapshot)
{
    Rl78ProtocolB *driver;
    driver = (Rl78ProtocolB *)context;
    if (driver == 0 || snapshot == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    memcpy(snapshot, &driver->failure_snapshot, sizeof(*snapshot));
    return PLP_OK;
}

uint32_t Rl78ProtocolB_Init(Rl78ProtocolB *driver,
                            const Rl78ProtocolBTransportOps *transport)
{
    if (driver == 0 || transport == 0 ||
        transport->enter_programming_mode == 0 ||
        transport->leave_safe == 0 || transport->restart_target == 0 ||
        transport->try_write_byte == 0 || transport->try_read_byte == 0 ||
        transport->is_transmit_idle == 0 || transport->now_ms == 0)
    {
        return PLP_ERR_DRIVER_PROFILE;
    }
    memset(driver, 0, sizeof(*driver));
    memcpy(&driver->transport, transport, sizeof(*transport));
    driver->step = RL78_B_STEP_IDLE;
    return PLP_OK;
}

void Rl78ProtocolB_GetTargetOps(Rl78ProtocolB *driver,
                                ProgrammerTargetOps *target)
{
    if (target == 0)
    {
        return;
    }
    memset(target, 0, sizeof(*target));
    if (driver == 0)
    {
        return;
    }
    target->context = driver;
    target->profile_ready = 1u;
    target->start = DriverStart;
    target->poll = DriverPoll;
    target->cancel = DriverCancel;
    target->retry_connect = DriverRetryConnect;
    target->get_failure_snapshot = DriverGetFailureSnapshot;
}

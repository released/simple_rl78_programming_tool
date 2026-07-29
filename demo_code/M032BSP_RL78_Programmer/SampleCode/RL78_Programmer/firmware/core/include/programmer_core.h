#ifndef RL78_PROGRAMMER_CORE_H
#define RL78_PROGRAMMER_CORE_H

#include <stdint.h>

#include "rl78_programmer/plp_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ProgrammerImageOpsTag;

typedef struct ProgrammerStorageOpsTag
{
    void *context;
    uint32_t (*begin)(void *context, const uint8_t package_uuid[16],
                      uint32_t total_size, uint32_t package_crc32,
                      const uint8_t hash_prefix[16], uint16_t *transfer_id,
                      uint32_t *accepted_offset);
    uint32_t (*write)(void *context, uint16_t transfer_id, uint32_t offset,
                      const uint8_t *data, uint8_t length,
                      uint32_t *accepted_offset);
    uint32_t (*finalize)(void *context, uint16_t transfer_id,
                         uint32_t total_size, const uint8_t package_sha256[32],
                         uint32_t package_crc32);
    uint32_t (*activate)(void *context, const uint8_t package_uuid[16],
                         uint32_t expected_generation, uint32_t *new_generation);
    uint32_t (*get_active)(void *context, uint8_t package_uuid[16],
                           uint32_t *generation);
    uint32_t (*open_image)(void *context, uint8_t source_kind,
                           const uint8_t package_uuid[16],
                           struct ProgrammerImageOpsTag *image);
} ProgrammerStorageOps;

typedef struct ProgrammerImageOpsTag
{
    void *context;
    uint8_t target_profile_uuid[16];
    uint32_t (*read_target)(void *context, uint32_t address,
                            uint8_t *data, uint32_t length);
    uint8_t (*has_region)(void *context, uint16_t section_type);
} ProgrammerImageOps;

typedef struct ProgrammerTargetStatusTag
{
    uint8_t stage;
    uint8_t terminal_result;
    uint16_t progress_basis_points;
    uint32_t current;
    uint32_t total;
    uint32_t error;
    uint16_t detail;
    uint16_t attempt;
    uint8_t terminal;
} ProgrammerTargetStatus;

typedef struct ProgrammerTargetFailureSnapshotTag
{
    uint8_t version;
    uint8_t valid;
    uint8_t protocol_step;
    uint8_t unit_byte_index;
    uint8_t tx_value;
    uint8_t tx_bit_index;
    uint8_t timer_state;
    uint8_t pin_flags;
    uint32_t address;
    uint16_t timer_count;
} ProgrammerTargetFailureSnapshot;

#define PROGRAMMER_OFFLINE_FAILURE_HISTORY_CAPACITY 64u

typedef struct ProgrammerOfflineFailureRecordTag
{
    uint32_t sequence;
    uint32_t captured_uptime_ms;
    uint32_t job_counter;
    uint32_t error;
    uint16_t attempt;
    uint16_t detail;
    uint8_t terminal_result;
    uint8_t stage;
    ProgrammerTargetFailureSnapshot snapshot;
} ProgrammerOfflineFailureRecord;

typedef struct ProgrammerTargetOpsTag
{
    void *context;
    uint8_t profile_ready;
    uint32_t (*read_signature)(void *context, const uint8_t requested_profile[16],
                               uint8_t matched_profile[16], uint8_t digest[16],
                               uint16_t *raw_length, uint16_t *flags);
    uint32_t (*start)(void *context, const ProgrammerImageOps *image,
                      uint8_t source_kind,
                      const uint8_t source_uuid[16],
                      const uint8_t target_profile_uuid[16], uint16_t flags);
    uint32_t (*poll)(void *context, ProgrammerTargetStatus *status);
    uint32_t (*cancel)(void *context, uint8_t *disposition);
    uint32_t (*retry_connect)(void *context);
    uint32_t (*get_failure_snapshot)(
        void *context, ProgrammerTargetFailureSnapshot *snapshot);
} ProgrammerTargetOps;

typedef struct ProgrammerModeOpsTag
{
    void *context;
    uint32_t (*load)(void *context, uint8_t *mode, uint8_t *autorun_enabled,
                     uint32_t *generation);
    uint32_t (*store)(void *context, uint8_t mode, uint8_t autorun_enabled,
                      uint32_t expected_generation, uint32_t *new_generation);
} ProgrammerModeOps;

typedef struct ProgrammerBootOpsTag
{
    void *context;
    /* Prepare bootloader entry and return without resetting so the ACK can be sent. */
    uint32_t (*prepare)(void *context);
    /* Schedule a normal programmer reset without invalidating the application. */
    uint32_t (*prepare_reset)(void *context);
} ProgrammerBootOps;

typedef struct ProgrammerCoreConfigTag
{
    const char *product_name;
    uint8_t programmer_uuid[16];
    uint8_t board_profile_uuid[16];
    uint8_t hardware_version[4];
    uint8_t firmware_version[8];
    uint32_t capabilities;
    uint32_t (*uptime_ms)(void *context);
    void *uptime_context;
    ProgrammerStorageOps storage;
    ProgrammerTargetOps target;
    ProgrammerModeOps mode;
    ProgrammerBootOps boot;
} ProgrammerCoreConfig;

typedef struct ProgrammerCoreTag
{
    ProgrammerCoreConfig config;
    uint8_t state;
    uint8_t negotiated;
    uint8_t session_open;
    uint8_t session_uuid[16];
    uint8_t transfer_package_uuid[16];
    uint8_t active_package_uuid[16];
    uint8_t active_job_uuid[16];
    uint8_t active_source_kind;
    uint16_t active_transfer_id;
    uint32_t active_generation;
    uint32_t mode_generation;
    uint32_t transfer_size;
    uint32_t transfer_offset;
    uint8_t operation_state;
    uint8_t terminal_result;
    uint8_t job_stage;
    uint8_t cancel_state;
    uint16_t progress_basis_points;
    uint16_t attempt;
    uint32_t job_current;
    uint32_t job_total;
    uint32_t job_elapsed_ms;
    uint32_t job_started_ms;
    uint32_t job_error;
    uint16_t job_detail;
    uint16_t last_command;
    uint16_t last_sequence;
    uint8_t last_response[PLP_REPORT_SIZE];
    uint8_t last_response_valid;
    uint8_t operating_mode;
    uint8_t offline_autorun_enabled;
    uint8_t last_offline_job_uuid[16];
    uint16_t last_offline_attempt;
    uint8_t last_offline_terminal_result;
    uint8_t last_offline_stage;
    uint32_t last_offline_error;
    uint16_t last_offline_detail;
    uint32_t last_offline_elapsed_ms;
    ProgrammerTargetFailureSnapshot last_offline_failure_snapshot;
    uint8_t last_offline_valid;
    ProgrammerOfflineFailureRecord offline_failure_history[
        PROGRAMMER_OFFLINE_FAILURE_HISTORY_CAPACITY];
    uint32_t offline_failure_sequence;
    uint32_t offline_failure_dropped;
    uint8_t offline_failure_head;
    uint8_t offline_failure_count;
} ProgrammerCore;

void ProgrammerCore_Init(ProgrammerCore *core, const ProgrammerCoreConfig *config);
uint32_t ProgrammerCore_HandleReport(ProgrammerCore *core,
                                     const uint8_t request[PLP_REPORT_SIZE],
                                     uint8_t response[PLP_REPORT_SIZE]);
uint32_t ProgrammerCore_StartOffline(ProgrammerCore *core,
                                     const uint8_t job_uuid[16]);
uint32_t ProgrammerCore_LoadOperatingMode(ProgrammerCore *core);
void ProgrammerCore_ReleaseSession(ProgrammerCore *core);
void ProgrammerCore_OnTransportDisconnected(ProgrammerCore *core);
void ProgrammerCore_Process(ProgrammerCore *core);

#ifdef __cplusplus
}
#endif

#endif

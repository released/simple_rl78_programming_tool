#ifndef RL78_PROGRAMMER_PLP_CONTRACT_H
#define RL78_PROGRAMMER_PLP_CONTRACT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PLP_MAGIC                       0xA5u
#define PLP_VERSION_MAJOR               1u
#define PLP_VERSION_MINOR               0u
#define PLP_REPORT_SIZE                 64u
#define PLP_HEADER_SIZE                 10u
#define PLP_PAYLOAD_SIZE                52u
#define PLP_CRC_OFFSET                  62u
#define PLP_COMMON_RESPONSE_SIZE        8u
#define PLP_RESPONSE_DATA_SIZE          44u
#define PLP_IMAGE_CHUNK_DATA_SIZE       45u

#define PLP_FLAG_RESPONSE               0x01u
#define PLP_FLAG_ACK                    0x02u
#define PLP_FLAG_NAK                    0x04u
#define PLP_FLAG_MORE                   0x08u
#define PLP_FLAG_EVENT                  0x10u
#define PLP_FLAG_RETRY                  0x20u
#define PLP_FLAG_RESERVED               0xC0u

#define PLP_RESPONSE_DATA_VALID         0x01u
#define PLP_RESPONSE_RETRYABLE          0x02u
#define PLP_RESPONSE_PARTIAL            0x04u

#define PLP_OFFLINE_FAILURE_SNAPSHOT_VERSION 1u
#define PLP_OFFLINE_FAILURE_HISTORY_VERSION  1u
#define PLP_OFFLINE_FAILURE_HISTORY_VALID    0x01u
#define PLP_OFFLINE_FAILURE_HISTORY_OVERFLOW 0x02u
#define PLP_OFFLINE_FAILURE_PIN_PA9_DOUT     0x01u
#define PLP_OFFLINE_FAILURE_PIN_PA9_INPUT    0x02u
#define PLP_OFFLINE_FAILURE_PIN_PA10_INPUT   0x04u
#define PLP_OFFLINE_FAILURE_PIN_PA10_IRQ     0x08u
#define PLP_OFFLINE_FAILURE_PIN_TX_BUSY      0x10u
#define PLP_OFFLINE_FAILURE_PIN_RX_ENABLED   0x20u
#define PLP_OFFLINE_FAILURE_DESTRUCTIVE      0x40u
#define PLP_OFFLINE_FAILURE_LINK_VALID       0x80u

#define PLP_STATE_BOOTING               0u
#define PLP_STATE_READY_UNNEGOTIATED    1u
#define PLP_STATE_READY                 2u
#define PLP_STATE_SESSION               3u
#define PLP_STATE_RECEIVING_IMAGE       4u
#define PLP_STATE_VALIDATING_IMAGE      5u
#define PLP_STATE_PROGRAMMING           6u
#define PLP_STATE_RECOVERING            7u
#define PLP_STATE_UPDATING              8u
#define PLP_STATE_STORAGE_RECOVERY      9u
#define PLP_STATE_ERROR_LATCHED         10u

#define PLP_OPERATION_QUEUED            0u
#define PLP_OPERATION_RUNNING           1u
#define PLP_OPERATION_RECONNECTING      2u
#define PLP_OPERATION_CANCEL_REQUESTED  3u
#define PLP_OPERATION_CANCEL_PENDING    4u
#define PLP_OPERATION_COMPLETED         5u
#define PLP_OPERATION_FAILED            6u
#define PLP_OPERATION_CANCELLED         7u

#define PLP_TERMINAL_NONE               0u
#define PLP_TERMINAL_PASS               1u
#define PLP_TERMINAL_FAIL               2u
#define PLP_TERMINAL_CANCELLED          3u
#define PLP_TERMINAL_INTERRUPTED        4u
#define PLP_TERMINAL_OUTCOME_UNKNOWN    5u

#define PLP_OPERATING_MODE_ONLINE_ONLY   0u
#define PLP_OPERATING_MODE_OFFLINE_ARMED 1u

#define PLP_CMD_NEGOTIATE               0x0001u
#define PLP_CMD_PING                    0x0002u
#define PLP_CMD_OPEN_SESSION            0x0003u
#define PLP_CMD_CLOSE_SESSION           0x0004u
#define PLP_CMD_RESET_LINK              0x0005u
#define PLP_CMD_GET_PROTOCOL_LIMITS     0x0006u
#define PLP_CMD_RESET_PROGRAMMER        0x0007u

#define PLP_CMD_GET_DEVICE_INFO         0x0100u
#define PLP_CMD_GET_CAPABILITIES        0x0101u
#define PLP_CMD_GET_STATUS              0x0102u
#define PLP_CMD_GET_LOG_PAGE            0x0103u
#define PLP_CMD_CLEAR_LOG               0x0104u
#define PLP_CMD_GET_STORAGE_INFO        0x0105u
#define PLP_CMD_GET_ACTIVE_PACKAGE      0x0106u
#define PLP_CMD_GET_LAST_OFFLINE_RESULT 0x0107u
#define PLP_CMD_GET_OPERATING_MODE      0x0108u
#define PLP_CMD_GET_OFFLINE_FAILURE_HISTORY 0x0109u

#define PLP_CMD_IMAGE_BEGIN             0x0200u
#define PLP_CMD_IMAGE_WRITE_CHUNK       0x0201u
#define PLP_CMD_IMAGE_FINALIZE          0x0202u
#define PLP_CMD_IMAGE_ACTIVATE          0x0203u
#define PLP_CMD_IMAGE_LIST              0x0204u
#define PLP_CMD_IMAGE_DELETE            0x0205u
#define PLP_CMD_IMAGE_GET_METADATA      0x0206u
#define PLP_CMD_IMAGE_ABORT             0x0207u
#define PLP_CMD_IMAGE_RECOVER           0x0208u

#define PLP_CMD_PROGRAM_START           0x0300u
#define PLP_CMD_PROGRAM_GET_JOB         0x0301u
#define PLP_CMD_PROGRAM_CANCEL          0x0302u
#define PLP_CMD_TARGET_READ_SIGNATURE   0x0303u
#define PLP_CMD_TARGET_GET_INFO         0x0304u
#define PLP_CMD_TARGET_READ_FLASH       0x0305u
#define PLP_CMD_TARGET_RESET            0x0306u
#define PLP_CMD_TARGET_RECOVER          0x0307u

#define PLP_CMD_CONFIG_GET              0x0400u
#define PLP_CMD_CONFIG_STAGE_BEGIN      0x0401u
#define PLP_CMD_CONFIG_STAGE_CHUNK      0x0402u
#define PLP_CMD_CONFIG_COMMIT           0x0403u
#define PLP_CMD_CONFIG_ROLLBACK         0x0404u
#define PLP_CMD_CONFIG_FACTORY_RESET    0x0405u
#define PLP_CMD_SET_OPERATING_MODE      0x0406u

#define PLP_CMD_FW_UPDATE_BEGIN         0x0500u
#define PLP_CMD_FW_UPDATE_CHUNK         0x0501u
#define PLP_CMD_FW_UPDATE_FINALIZE      0x0502u
#define PLP_CMD_FW_UPDATE_APPLY         0x0503u
#define PLP_CMD_FW_UPDATE_STATUS        0x0504u
#define PLP_CMD_FW_UPDATE_ABORT         0x0505u
#define PLP_CMD_FW_ENTER_BOOTLOADER     0x0506u

#define PLP_CMD_PRODUCTION_GET_STATS    0x0600u
#define PLP_CMD_PRODUCTION_GET_HISTORY  0x0601u
#define PLP_CMD_PRODUCTION_RESET_STATS  0x0602u
#define PLP_CMD_PRODUCTION_SET_CONTEXT  0x0603u
#define PLP_CMD_PRODUCTION_GET_COUNTERS 0x0604u

#define PLP_EVENT_STATE_CHANGED         0x7000u
#define PLP_EVENT_JOB_PROGRESS          0x7001u
#define PLP_EVENT_JOB_RESULT            0x7002u
#define PLP_EVENT_LOG_AVAILABLE         0x7003u
#define PLP_EVENT_DEVICE_REBOOTING      0x7004u
#define PLP_EVENT_OFFLINE_RESULT        0x7005u
#define PLP_EVENT_STORAGE_CHANGED       0x7006u

#define PLP_CAP_REPLAY_CACHE            (1ul << 0)
#define PLP_CAP_ASYNC_EVENTS            (1ul << 1)
#define PLP_CAP_PAGED_QUERY             (1ul << 2)
#define PLP_CAP_RECONNECT_QUERY         (1ul << 3)
#define PLP_CAP_TRANSFER_RESUME         (1ul << 4)
#define PLP_CAP_OFFLINE_PACKAGE         (1ul << 5)
#define PLP_CAP_TARGET_PROGRAM          (1ul << 6)
#define PLP_CAP_TARGET_INFORMATION      (1ul << 7)
#define PLP_CAP_TARGET_READBACK         (1ul << 8)
#define PLP_CAP_CONFIG_SNAPSHOT         (1ul << 9)
#define PLP_CAP_PROGRAMMER_UPDATE       (1ul << 10)
#define PLP_CAP_DURABLE_HISTORY         (1ul << 11)
#define PLP_CAP_STATISTICS              (1ul << 12)
#define PLP_CAP_PRODUCTION_CONTEXT      (1ul << 13)
#define PLP_CAP_PACKAGE_SIGNATURE       (1ul << 14)
#define PLP_CAP_ANTI_ROLLBACK           (1ul << 15)
#define PLP_CAP_ATOMIC_ACTIVATION       (1ul << 16)
#define PLP_CAP_STORAGE_AB              (1ul << 17)
#define PLP_CAP_MULTI_IMAGE             (1ul << 18)
#define PLP_CAP_COMPRESSION             (1ul << 19)
#define PLP_CAP_ENCRYPTION              (1ul << 20)
#define PLP_CAP_OFFLINE_BUTTON          (1ul << 21)
#define PLP_CAP_OFFLINE_AUTORUN         (1ul << 22)
#define PLP_CAP_AUTH_SESSION            (1ul << 23)
#define PLP_CAP_STRUCTURED_LOG          (1ul << 24)
#define PLP_CAP_TARGET_RECOVERY         (1ul << 25)
#define PLP_CAP_PROGRAMMER_RESET        (1ul << 26)

#define PLP_OK                          0x00000000ul
#define PLP_ERR_SYSTEM_INTERNAL         0x30103001ul
#define PLP_ERR_SYSTEM_RESOURCE         0x20100002ul
#define PLP_ERR_SYSTEM_WATCHDOG         0x30103003ul
#define PLP_ERR_USB_NOT_FOUND           0x20207001ul
#define PLP_ERR_USB_ACCESS              0x20207002ul
#define PLP_ERR_USB_REMOVED             0x20207003ul
#define PLP_ERR_USB_TIMEOUT             0x20207004ul
#define PLP_ERR_BAD_MAGIC               0x20301001ul
#define PLP_ERR_BAD_LENGTH              0x20301002ul
#define PLP_ERR_CRC                     0x20301003ul
#define PLP_ERR_BAD_FLAGS               0x20301004ul
#define PLP_ERR_SEQUENCE_CONFLICT       0x20302001ul
#define PLP_ERR_VERSION                 0x20302002ul
#define PLP_ERR_COMMAND_UNSUPPORTED     0x20303001ul
#define PLP_ERR_CAPABILITY_UNSUPPORTED  0x20303002ul
#define PLP_ERR_INVALID_STATE           0x20303003ul
#define PLP_ERR_CURSOR_STALE            0x20303004ul
#define PLP_ERR_UNKNOWN_REMOTE_DETAIL   0x20303005ul
#define PLP_ERR_IMAGE_FORMAT            0x20401001ul
#define PLP_ERR_IMAGE_CHECKSUM          0x20401002ul
#define PLP_ERR_IMAGE_ADDRESS_OVERFLOW  0x20408001ul
#define PLP_ERR_IMAGE_ADDRESS           0x20408002ul
#define PLP_ERR_IMAGE_OVERLAP           0x20408003ul
#define PLP_ERR_IMAGE_DUPLICATE         0x20408004ul
#define PLP_ERR_IMAGE_OPTION_POLICY     0x20408005ul
#define PLP_ERR_IMAGE_INCOMPATIBLE      0x20408006ul
#define PLP_ERR_INTEGRITY_CRC           0x20508001ul
#define PLP_ERR_INTEGRITY_HASH          0x20508002ul
#define PLP_ERR_SIGNATURE_REQUIRED      0x20508003ul
#define PLP_ERR_SIGNATURE_INVALID       0x20508004ul
#define PLP_ERR_UNAUTHORIZED            0x20508005ul
#define PLP_ERR_ROLLBACK                0x20508006ul
#define PLP_ERR_STORAGE_CAPACITY        0x20605001ul
#define PLP_ERR_STORAGE_OFFSET          0x20605002ul
#define PLP_ERR_STORAGE_WRITE           0x20605003ul
#define PLP_ERR_STORAGE_JOURNAL         0x20609001ul
#define PLP_ERR_STORAGE_NO_IMAGE        0x20605004ul
#define PLP_ERR_PROGRAM_BUSY            0x20703001ul
#define PLP_ERR_PROGRAM_JOB_CONFLICT    0x20703002ul
#define PLP_WARN_CANCEL_PENDING         0x10703003ul
#define PLP_ERR_VERIFY_MISMATCH         0x20704001ul
#define PLP_ERR_OUTCOME_UNKNOWN         0x20703004ul
#define PLP_ERR_DRIVER_PROFILE          0x20806001ul
#define PLP_ERR_DRIVER_TARGET_MISMATCH  0x20806002ul
#define PLP_ERR_DRIVER_TARGET_STATUS    0x20806003ul
#define PLP_ERR_TARGET_TIMEOUT          0x20907001ul
#define PLP_ERR_TARGET_FRAMING          0x20907002ul
#define PLP_ERR_TARGET_DIRECTION        0x20907003ul
#define PLP_ERR_OFFLINE_NOT_ACTIVE      0x20A05001ul
#define PLP_ERR_OFFLINE_TRIGGER         0x20A03001ul
#define PLP_ERR_CONFIG_SCHEMA           0x20B08001ul
#define PLP_ERR_CONFIG_VALIDATION       0x20B08002ul
#define PLP_ERR_CONFIG_PERSIST          0x20B09001ul
#define PLP_ERR_UPDATE_INCOMPATIBLE     0x20C03001ul
#define PLP_ERR_UPDATE_APPLY            0x20C03002ul
#define PLP_ERR_BARCODE                 0x20D0A001ul
#define PLP_ERR_HISTORY_COMMIT          0x20D09001ul
#define PLP_ERR_MES                     0x20D0A002ul
#define PLP_ERR_PLUGIN_ABI              0x20E03001ul
#define PLP_ERR_PLUGIN_TRUST            0x20E03002ul
#define PLP_ERR_SDK_ARGUMENT            0x20F00001ul
#define PLP_ERR_SDK_HANDLE              0x20F00002ul
#define PLP_ERR_SDK_BUSY                0x20F00003ul
#define PLP_ERR_FILE_PERMISSION         0x21007001ul
#define PLP_ERR_FILE_DISK_FULL          0x21007002ul
#define PLP_ERR_UART_NOT_FOUND          0x21107001ul
#define PLP_ERR_UART_ACCESS             0x21107002ul
#define PLP_ERR_UART_REMOVED            0x21107003ul
#define PLP_ERR_UART_TIMEOUT            0x21107004ul
#define PLP_ERR_UNKNOWN                 0x20100FFFul

#ifdef __cplusplus
}
#endif

#endif

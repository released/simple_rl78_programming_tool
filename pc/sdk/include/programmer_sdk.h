#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "rl78_programmer/offline_image.h"
#include "rl78_programmer/plp_contract.h"

namespace rl78_programmer::sdk {

enum class TransportKind : std::uint8_t {
    UsbHid = 0,
    Uart = 1
};

struct DeviceDescriptor {
    std::wstring path;
    std::wstring manufacturer;
    std::wstring product;
    std::wstring serial_number;
    std::uint16_t vendor_id = 0;
    std::uint16_t product_id = 0;
    std::uint16_t usage_page = 0;
    std::uint16_t usage = 0;
    std::uint16_t input_report_length = 0;
    std::uint16_t output_report_length = 0;
    TransportKind transport = TransportKind::UsbHid;
    std::uint32_t baud_rate = 0;
};

struct ProgrammerInfo {
    std::array<std::uint8_t, 16> uuid{};
    std::wstring product_name = L"Unknown programmer";
    std::wstring hardware_version = L"-";
    std::wstring firmware_version = L"-";
    std::uint32_t health_flags = 0;
};

struct ProgrammerStatus {
    std::uint8_t state = 0;
    std::uint32_t health_flags = 0;
    std::array<std::uint8_t, 16> session_uuid{};
    std::array<std::uint8_t, 16> active_job_uuid{};
    std::uint16_t active_transfer_id = 0;
    std::uint8_t active_slot = 0xFFu;
};

struct OperatingModeInfo {
    std::uint8_t mode = PLP_OPERATING_MODE_ONLINE_ONLY;
    bool autorun_enabled = false;
    bool active_package_available = false;
    std::uint32_t generation = 0;
    std::array<std::uint8_t, 16> active_package_uuid{};
};

struct OfflineFailureSnapshot {
    std::uint8_t version = 0;
    bool valid = false;
    std::uint8_t protocol_step = 0;
    std::uint8_t unit_byte_index = 0;
    std::uint8_t tx_value = 0;
    std::uint8_t tx_bit_index = 0;
    std::uint8_t timer_state = 0;
    std::uint8_t pin_flags = 0;
    std::uint32_t address = 0;
    std::uint16_t timer_count = 0;
};

struct OfflineResult {
    bool available = false;
    std::array<std::uint8_t, 16> job_uuid{};
    std::uint16_t attempt = 0;
    std::uint8_t terminal_result = 0;
    std::uint8_t stage = 0;
    std::uint32_t error = 0;
    std::uint16_t detail = 0;
    std::uint32_t elapsed_ms = 0;
    OfflineFailureSnapshot failure;
};

struct OfflineFailureRecord {
    std::uint8_t schema = 0;
    std::uint32_t sequence = 0;
    std::uint32_t next_sequence = 0;
    std::uint32_t job_counter = 0;
    std::uint32_t captured_uptime_ms = 0;
    std::uint16_t attempt = 0;
    std::uint8_t terminal_result = 0;
    std::uint8_t stage = 0;
    std::uint32_t error = 0;
    std::uint16_t detail = 0;
    OfflineFailureSnapshot failure;
};

struct OfflineFailureHistory {
    bool overflowed = false;
    std::uint16_t dropped = 0;
    std::vector<OfflineFailureRecord> records;
};

struct TargetSignature {
    std::array<std::uint8_t, 16> matched_profile_uuid{};
    std::array<std::uint8_t, 16> signature_digest{};
    std::uint16_t raw_signature_length = 0;
    std::uint16_t flags = 0;
};

struct JobStatus {
    std::array<std::uint8_t, 16> job_uuid{};
    std::uint8_t operation_state = 0;
    std::uint8_t stage = 0;
    std::uint8_t terminal_result = 0;
    std::uint8_t cancel_state = 0;
    std::uint16_t progress_basis_points = 0;
    std::uint16_t attempt = 0;
    std::uint32_t current = 0;
    std::uint32_t total = 0;
    std::uint32_t elapsed_ms = 0;
    std::uint32_t error = 0;
    std::uint16_t detail = 0;
};

class ProgrammerException : public std::runtime_error {
public:
    ProgrammerException(std::uint32_t status, const std::string& message);
    [[nodiscard]] std::uint32_t status() const noexcept { return status_; }

private:
    std::uint32_t status_;
};

using ProgressCallback = std::function<void(std::uint16_t basis_points,
                                            const std::wstring& message)>;

class ProgrammerClient {
public:
    ProgrammerClient();
    ~ProgrammerClient();

    ProgrammerClient(const ProgrammerClient&) = delete;
    ProgrammerClient& operator=(const ProgrammerClient&) = delete;

    static std::vector<DeviceDescriptor> Enumerate(std::uint16_t vendor_id,
                                                    std::uint16_t product_id,
                                                    bool filter_ids = true);
    static std::vector<DeviceDescriptor> EnumerateSerial();
    static std::array<std::uint8_t, 16> G15ProfileUuid();

    void Connect(const DeviceDescriptor& descriptor, int timeout_ms = 1000);
    void Disconnect() noexcept;
    [[nodiscard]] bool IsConnected() const noexcept;
    [[nodiscard]] DeviceDescriptor CurrentDevice() const;
    [[nodiscard]] std::uint32_t Capabilities() const noexcept;

    std::uint32_t Ping(std::uint32_t echo_token = 0x52505031u);
    ProgrammerInfo GetDeviceInfo();
    ProgrammerStatus GetStatus();
    OfflineResult GetLastOfflineResult();
    OfflineFailureHistory GetOfflineFailureHistory();
    OperatingModeInfo GetOperatingMode();
    OperatingModeInfo SetOperatingMode(std::uint8_t mode,
                                       bool autorun_enabled,
                                       std::uint32_t expected_generation);
    void ResetProgrammer();
    void EnterBootloader();
    TargetSignature ReadTargetSignature(const std::array<std::uint8_t, 16>& profile_uuid);

    void DownloadPackage(const OfflineImagePackage& package,
                         const ProgressCallback& progress = {});
    void ActivatePackage(const std::array<std::uint8_t, 16>& package_uuid,
                         std::uint32_t expected_generation = 0);
    std::array<std::uint8_t, 16> StartProgram(
        std::uint8_t source_kind,
        const std::array<std::uint8_t, 16>& source_uuid,
        const std::array<std::uint8_t, 16>& profile_uuid,
        std::uint16_t program_flags = 0);
    JobStatus QueryJob(const std::array<std::uint8_t, 16>& job_uuid);
    std::uint8_t CancelJob(const std::array<std::uint8_t, 16>& job_uuid,
                           std::uint16_t reason = 1);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

std::wstring FormatUuid(const std::array<std::uint8_t, 16>& uuid);
std::wstring StatusName(std::uint8_t state);
std::wstring StageName(std::uint8_t stage);
std::wstring TerminalResultName(std::uint8_t result);
std::wstring ErrorName(std::uint32_t status);

} // namespace rl78_programmer::sdk

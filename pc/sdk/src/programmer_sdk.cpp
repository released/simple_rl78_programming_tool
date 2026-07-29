#include "programmer_sdk.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>

#include "hid_transport.h"
#include "programmer_transport.h"
#include "rl78_programmer/integrity.h"
#include "rl78_programmer/oip_format.h"
#include "rl78_programmer/plp_contract.h"
#include "rl78_programmer/plp_frame.h"
#include "serial_transport.h"

namespace rl78_programmer::sdk {
namespace {

std::array<std::uint8_t, 16> NewOperationUuid(std::uint8_t domain) {
    std::array<std::uint8_t, 16> value{};
    const auto now = static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const auto process = static_cast<std::uint32_t>(GetCurrentProcessId());
    const auto thread = static_cast<std::uint32_t>(GetCurrentThreadId());
    for (std::size_t i = 0; i < 8; ++i) value[i] = static_cast<std::uint8_t>(now >> (i * 8));
    Rpp_WriteLe32(value.data() + 8, process);
    Rpp_WriteLe32(value.data() + 12, thread ^ domain ^ GetTickCount());
    value[6] = static_cast<std::uint8_t>((value[6] & 0x0Fu) | 0x40u);
    value[8] = static_cast<std::uint8_t>((value[8] & 0x3Fu) | 0x80u);
    return value;
}

std::wstring Utf8ToWide(const std::uint8_t* data, std::size_t size) {
    if (size == 0) return L"";
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                          reinterpret_cast<const char*>(data),
                                          static_cast<int>(size), nullptr, 0);
    if (count <= 0) return L"<invalid UTF-8>";
    std::wstring out(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                        reinterpret_cast<const char*>(data), static_cast<int>(size),
                        out.data(), count);
    return out;
}

std::wstring Version4(const std::uint8_t* value) {
    std::wostringstream out;
    out << static_cast<unsigned>(value[0]) << L'.'
        << static_cast<unsigned>(value[1]) << L'.'
        << static_cast<unsigned>(value[2]);
    return out.str();
}

OfflineFailureSnapshot DecodeOfflineFailureSnapshot(
    const std::uint8_t* value) {
    OfflineFailureSnapshot snapshot;
    snapshot.version = value[0];
    snapshot.valid = value[1] != 0;
    snapshot.protocol_step = value[2];
    snapshot.unit_byte_index = value[3];
    snapshot.tx_value = value[4];
    snapshot.tx_bit_index = value[5];
    snapshot.timer_state = value[6];
    snapshot.pin_flags = value[7];
    snapshot.address = Rpp_ReadLe32(value + 8);
    snapshot.timer_count = Rpp_ReadLe16(value + 12);
    return snapshot;
}

bool IsTransportTimeout(std::uint32_t status) {
    return status == PLP_ERR_USB_TIMEOUT || status == PLP_ERR_UART_TIMEOUT;
}

} // namespace

class ProgrammerClient::Impl {
public:
    std::vector<std::uint8_t> Transact(std::uint16_t command,
                                       const std::vector<std::uint8_t>& payload,
                                       bool replay_safe = true) {
        std::lock_guard<std::mutex> guard(mutex_);
        if (transport_ == nullptr || !transport_->IsOpen()) {
            throw ProgrammerException(PLP_ERR_SDK_HANDLE,
                                      "programmer is not connected");
        }
        if (payload.size() > PLP_PAYLOAD_SIZE) {
            throw ProgrammerException(PLP_ERR_BAD_LENGTH, "PLP payload exceeds 52 bytes");
        }
        std::uint16_t sequence = next_sequence_++;
        if (next_sequence_ == 0) next_sequence_ = 1;
        const int attempts = replay_safe ? 3 : 1;
        for (int attempt = 0; attempt < attempts; ++attempt) {
            std::array<std::uint8_t, PLP_REPORT_SIZE> request{};
            const std::uint8_t flags = attempt == 0 ? 0 : PLP_FLAG_RETRY;
            const auto encode = Rpp_PlpEncode(PLP_VERSION_MAJOR, PLP_VERSION_MINOR, flags,
                                              command, sequence,
                                              payload.empty() ? nullptr : payload.data(),
                                              static_cast<std::uint16_t>(payload.size()),
                                              request.data());
            if (encode != PLP_OK) {
                throw ProgrammerException(encode, "failed to encode PLP request");
            }
            try {
                transport_->Write(request);
                const auto deadline = std::chrono::steady_clock::now() +
                                      std::chrono::milliseconds(timeout_ms_);
                while (std::chrono::steady_clock::now() < deadline) {
                    auto report = transport_->Read(timeout_ms_);
                    RppPlpFrame frame{};
                    const auto decode = Rpp_PlpDecode(report.data(), &frame);
                    if (decode != PLP_OK) {
                        throw ProgrammerException(decode, "invalid PLP response");
                    }
                    if ((frame.flags & PLP_FLAG_EVENT) != 0) {
                        continue;
                    }
                    if (frame.command != command || frame.sequence != sequence) {
                        continue;
                    }
                    if ((frame.flags & PLP_FLAG_RESPONSE) == 0 ||
                        frame.payload_length < PLP_COMMON_RESPONSE_SIZE) {
                        throw ProgrammerException(PLP_ERR_BAD_FLAGS, "PLP response prefix is invalid");
                    }
                    const std::uint32_t status = Rpp_ReadLe32(frame.payload);
                    current_state_ = frame.payload[4];
                    if (status != PLP_OK || (frame.flags & PLP_FLAG_NAK) != 0) {
                        throw ProgrammerException(status == 0 ? PLP_ERR_UNKNOWN : status,
                                                  "programmer rejected command");
                    }
                    return std::vector<std::uint8_t>(frame.payload + PLP_COMMON_RESPONSE_SIZE,
                                                     frame.payload + frame.payload_length);
                }
                const auto status =
                    transport_->CurrentDevice().transport ==
                            TransportKind::Uart
                        ? PLP_ERR_UART_TIMEOUT
                        : PLP_ERR_USB_TIMEOUT;
                throw ProgrammerException(status, "PLP response timeout");
            } catch (const ProgrammerException& ex) {
                if (!IsTransportTimeout(ex.status()) ||
                    attempt + 1 >= attempts) {
                    throw;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100u << attempt));
            }
        }
        const auto status =
            transport_->CurrentDevice().transport == TransportKind::Uart
                ? PLP_ERR_UART_TIMEOUT
                : PLP_ERR_USB_TIMEOUT;
        throw ProgrammerException(status, "PLP retry limit reached");
    }

    void NegotiateAndOpenSession() {
        std::vector<std::uint8_t> request(28, std::uint8_t{0});
        request[0] = PLP_VERSION_MAJOR;
        request[1] = PLP_VERSION_MINOR;
        request[2] = PLP_VERSION_MAJOR;
        request[3] = PLP_VERSION_MINOR;
        const std::uint32_t host_caps = PLP_CAP_REPLAY_CACHE | PLP_CAP_PAGED_QUERY |
                                        PLP_CAP_RECONNECT_QUERY | PLP_CAP_TRANSFER_RESUME |
                                        PLP_CAP_OFFLINE_PACKAGE | PLP_CAP_TARGET_PROGRAM |
                                        PLP_CAP_TARGET_INFORMATION |
                                        PLP_CAP_PROGRAMMER_UPDATE |
                                        PLP_CAP_PROGRAMMER_RESET;
        Rpp_WriteLe32(request.data() + 4, host_caps);
        request[8] = 1;
        request[9] = 4;
        request[10] = 1;
        const auto host_uuid = NewOperationUuid(1);
        std::copy(host_uuid.begin(), host_uuid.end(), request.begin() + 12);
        auto response = Transact(PLP_CMD_NEGOTIATE, request);
        if (response.size() < 32 || response[0] != PLP_VERSION_MAJOR) {
            throw ProgrammerException(PLP_ERR_VERSION, "no compatible PLP version");
        }
        capabilities_ = Rpp_ReadLe32(response.data() + 8);
        std::copy_n(response.begin() + 16, 16, programmer_uuid_.begin());

        client_session_uuid_ = NewOperationUuid(2);
        request.assign(36, std::uint8_t{0});
        std::copy(client_session_uuid_.begin(), client_session_uuid_.end(), request.begin());
        request[16] = 1;
        response = Transact(PLP_CMD_OPEN_SESSION, request);
        if (response.size() < 24) {
            throw ProgrammerException(PLP_ERR_BAD_LENGTH, "OPEN_SESSION response is truncated");
        }
        std::copy_n(response.begin(), 16, programmer_session_uuid_.begin());
    }

    void CloseWithoutCommand() noexcept {
        if (transport_ != nullptr) {
            transport_->Close();
            transport_.reset();
        }
        current_state_ = PLP_STATE_READY_UNNEGOTIATED;
        capabilities_ = 0;
        programmer_uuid_.fill(0);
        client_session_uuid_.fill(0);
        programmer_session_uuid_.fill(0);
    }

    std::vector<std::uint8_t> ReadPage(std::uint16_t command,
                                       const std::vector<std::uint8_t>& prefix = {}) {
        std::vector<std::uint8_t> all;
        std::uint32_t snapshot = 0;
        std::uint32_t cursor = 0;
        for (unsigned page = 0; page < 1024; ++page) {
            std::vector<std::uint8_t> request(prefix.size() + 12, std::uint8_t{0});
            std::copy(prefix.begin(), prefix.end(), request.begin());
            Rpp_WriteLe32(request.data() + prefix.size(), snapshot);
            Rpp_WriteLe32(request.data() + prefix.size() + 4, cursor);
            request[prefix.size() + 8] = 35;
            auto response = Transact(command, request);
            if (response.size() < 9) {
                throw ProgrammerException(PLP_ERR_BAD_LENGTH, "paged response is truncated");
            }
            const std::uint32_t response_snapshot = Rpp_ReadLe32(response.data());
            const std::uint32_t next_cursor = Rpp_ReadLe32(response.data() + 4);
            const std::uint8_t length = response[8];
            if (response.size() != 9u + length || length > 35) {
                throw ProgrammerException(PLP_ERR_BAD_LENGTH, "paged response length is invalid");
            }
            if (snapshot != 0 && response_snapshot != snapshot) {
                throw ProgrammerException(PLP_ERR_CURSOR_STALE, "paged snapshot changed");
            }
            snapshot = response_snapshot;
            all.insert(all.end(), response.begin() + 9, response.end());
            if (next_cursor == 0 || next_cursor == cursor) break;
            cursor = next_cursor;
        }
        return all;
    }

    std::unique_ptr<detail::ProgrammerTransport> transport_;
    mutable std::mutex mutex_;
    int timeout_ms_ = 1000;
    std::uint16_t next_sequence_ = 1;
    std::uint8_t current_state_ = PLP_STATE_READY_UNNEGOTIATED;
    std::uint32_t capabilities_ = 0;
    std::array<std::uint8_t, 16> programmer_uuid_{};
    std::array<std::uint8_t, 16> client_session_uuid_{};
    std::array<std::uint8_t, 16> programmer_session_uuid_{};
};

ProgrammerClient::ProgrammerClient() : impl_(std::make_unique<Impl>()) {}
ProgrammerClient::~ProgrammerClient() { Disconnect(); }

std::vector<DeviceDescriptor> ProgrammerClient::Enumerate(std::uint16_t vendor_id,
                                                          std::uint16_t product_id,
                                                          bool filter_ids) {
    return detail::HidTransport::Enumerate(vendor_id, product_id, filter_ids);
}

std::vector<DeviceDescriptor> ProgrammerClient::EnumerateSerial() {
    return detail::SerialTransport::Enumerate();
}

std::array<std::uint8_t, 16> ProgrammerClient::G15ProfileUuid() {
    return Rl78G15_8KTargetProfile().uuid;
}

void ProgrammerClient::Connect(const DeviceDescriptor& descriptor, int timeout_ms) {
    Disconnect();
    if (descriptor.transport == TransportKind::Uart) {
        auto transport = std::make_unique<detail::SerialTransport>();
        transport->Open(descriptor, timeout_ms);
        impl_->transport_ = std::move(transport);
    } else {
        auto transport = std::make_unique<detail::HidTransport>();
        transport->Open(descriptor, timeout_ms);
        impl_->transport_ = std::move(transport);
    }
    impl_->timeout_ms_ = (std::max)(timeout_ms, 100);
    try {
        impl_->NegotiateAndOpenSession();
    } catch (...) {
        impl_->CloseWithoutCommand();
        throw;
    }
}

void ProgrammerClient::Disconnect() noexcept {
    if (impl_->transport_ != nullptr && impl_->transport_->IsOpen()) {
        try {
            std::vector<std::uint8_t> request(20, std::uint8_t{0});
            std::copy(impl_->programmer_session_uuid_.begin(),
                      impl_->programmer_session_uuid_.end(), request.begin());
            static_cast<void>(impl_->Transact(PLP_CMD_CLOSE_SESSION, request));
        } catch (...) {
        }
    }
    impl_->CloseWithoutCommand();
}

bool ProgrammerClient::IsConnected() const noexcept {
    return impl_->transport_ != nullptr && impl_->transport_->IsOpen();
}
DeviceDescriptor ProgrammerClient::CurrentDevice() const {
    return impl_->transport_ != nullptr
               ? impl_->transport_->CurrentDevice()
               : DeviceDescriptor{};
}
std::uint32_t ProgrammerClient::Capabilities() const noexcept { return impl_->capabilities_; }

std::uint32_t ProgrammerClient::Ping(std::uint32_t echo_token) {
    std::vector<std::uint8_t> request(4);
    Rpp_WriteLe32(request.data(), echo_token);
    const auto response = impl_->Transact(PLP_CMD_PING, request);
    if (response.size() < 4 || Rpp_ReadLe32(response.data()) != echo_token) {
        throw ProgrammerException(PLP_ERR_BAD_LENGTH, "PING echo does not match");
    }
    return response.size() >= 8 ? Rpp_ReadLe32(response.data() + 4) : 0;
}

ProgrammerInfo ProgrammerClient::GetDeviceInfo() {
    const auto data = impl_->ReadPage(PLP_CMD_GET_DEVICE_INFO);
    ProgrammerInfo info;
    for (std::size_t offset = 0; offset + 4 <= data.size();) {
        const std::uint16_t type = Rpp_ReadLe16(data.data() + offset);
        const std::uint8_t length = data[offset + 3];
        if (offset + 4u + length > data.size()) break;
        const auto* value = data.data() + offset + 4;
        switch (type) {
        case 0x0001: if (length == 16) std::copy_n(value, 16, info.uuid.begin()); break;
        case 0x0002: info.product_name = Utf8ToWide(value, length); break;
        case 0x0003: if (length >= 4) info.hardware_version = Version4(value); break;
        case 0x0004:
            if (length >= 8) {
                std::wostringstream out;
                out << Version4(value) << L'.' << Rpp_ReadLe32(value + 4);
                info.firmware_version = out.str();
            }
            break;
        case 0x0009: if (length >= 4) info.health_flags = Rpp_ReadLe32(value); break;
        default: break;
        }
        offset += 4u + length;
    }
    return info;
}

ProgrammerStatus ProgrammerClient::GetStatus() {
    const auto response = impl_->Transact(PLP_CMD_GET_STATUS, {});
    if (response.size() < 44) {
        throw ProgrammerException(PLP_ERR_BAD_LENGTH, "GET_STATUS response is truncated");
    }
    ProgrammerStatus status;
    status.state = response[0];
    status.health_flags = Rpp_ReadLe32(response.data() + 1);
    std::copy_n(response.begin() + 5, 16, status.session_uuid.begin());
    std::copy_n(response.begin() + 21, 16, status.active_job_uuid.begin());
    status.active_transfer_id = Rpp_ReadLe16(response.data() + 37);
    status.active_slot = response[39];
    return status;
}

OfflineResult ProgrammerClient::GetLastOfflineResult() {
    const auto response = impl_->Transact(PLP_CMD_GET_LAST_OFFLINE_RESULT, {});
    if (response.size() < 44) {
        throw ProgrammerException(PLP_ERR_BAD_LENGTH,
                                  "GET_LAST_OFFLINE_RESULT response is truncated");
    }
    OfflineResult result;
    std::copy_n(response.begin(), 16, result.job_uuid.begin());
    result.available = std::any_of(result.job_uuid.begin(), result.job_uuid.end(),
                                   [](std::uint8_t value) { return value != 0; });
    result.attempt = Rpp_ReadLe16(response.data() + 16);
    result.terminal_result = response[18];
    result.stage = response[19];
    result.error = Rpp_ReadLe32(response.data() + 20);
    result.detail = Rpp_ReadLe16(response.data() + 24);
    result.elapsed_ms = Rpp_ReadLe32(response.data() + 26);
    result.failure = DecodeOfflineFailureSnapshot(response.data() + 30);
    return result;
}

OfflineFailureHistory ProgrammerClient::GetOfflineFailureHistory() {
    OfflineFailureHistory history;
    std::uint32_t cursor = 0;
    for (unsigned page = 0; page < 128; ++page) {
        std::vector<std::uint8_t> request(4, std::uint8_t{0});
        Rpp_WriteLe32(request.data(), cursor);
        const auto response =
            impl_->Transact(PLP_CMD_GET_OFFLINE_FAILURE_HISTORY, request);
        if (response.size() < 44) {
            throw ProgrammerException(
                PLP_ERR_BAD_LENGTH,
                "GET_OFFLINE_FAILURE_HISTORY response is truncated");
        }
        if (response[11] != PLP_OFFLINE_FAILURE_HISTORY_VERSION) {
            throw ProgrammerException(
                PLP_ERR_VERSION,
                "GET_OFFLINE_FAILURE_HISTORY schema is unsupported");
        }
        const auto flags = response[10];
        history.overflowed = history.overflowed ||
            (flags & PLP_OFFLINE_FAILURE_HISTORY_OVERFLOW) != 0u;
        const auto dropped = Rpp_ReadLe16(response.data() + 8);
        if (dropped > history.dropped) history.dropped = dropped;
        if ((flags & PLP_OFFLINE_FAILURE_HISTORY_VALID) == 0u) break;

        OfflineFailureRecord record;
        record.schema = response[11];
        record.sequence = Rpp_ReadLe32(response.data());
        record.next_sequence = Rpp_ReadLe32(response.data() + 4);
        record.job_counter = Rpp_ReadLe32(response.data() + 12);
        record.captured_uptime_ms = Rpp_ReadLe32(response.data() + 16);
        record.attempt = Rpp_ReadLe16(response.data() + 20);
        record.terminal_result = response[22];
        record.stage = response[23];
        record.error = Rpp_ReadLe32(response.data() + 24);
        record.detail = Rpp_ReadLe16(response.data() + 28);
        record.failure = DecodeOfflineFailureSnapshot(response.data() + 30);
        history.records.push_back(record);
        if (record.next_sequence == 0u) break;
        if (record.next_sequence <= record.sequence) {
            throw ProgrammerException(
                PLP_ERR_SEQUENCE_CONFLICT,
                "GET_OFFLINE_FAILURE_HISTORY cursor did not advance");
        }
        cursor = record.next_sequence;
    }
    return history;
}

namespace {

OperatingModeInfo DecodeOperatingMode(const std::vector<std::uint8_t>& response) {
    if (response.size() < 24) {
        throw ProgrammerException(PLP_ERR_BAD_LENGTH, "operating mode response is truncated");
    }
    OperatingModeInfo info;
    info.mode = response[0];
    info.autorun_enabled = response[1] != 0;
    info.active_package_available = response[2] != 0;
    info.generation = Rpp_ReadLe32(response.data() + 4);
    std::copy_n(response.begin() + 8, 16, info.active_package_uuid.begin());
    return info;
}

} // namespace

OperatingModeInfo ProgrammerClient::GetOperatingMode() {
    return DecodeOperatingMode(impl_->Transact(PLP_CMD_GET_OPERATING_MODE, {}));
}

OperatingModeInfo ProgrammerClient::SetOperatingMode(std::uint8_t mode,
                                                      bool autorun_enabled,
                                                      std::uint32_t expected_generation) {
    std::vector<std::uint8_t> request(8, std::uint8_t{0});
    request[0] = mode;
    request[1] = autorun_enabled ? 1u : 0u;
    Rpp_WriteLe32(request.data() + 4, expected_generation);
    return DecodeOperatingMode(impl_->Transact(PLP_CMD_SET_OPERATING_MODE, request));
}

void ProgrammerClient::ResetProgrammer() {
    static_cast<void>(impl_->Transact(PLP_CMD_RESET_PROGRAMMER, {}, false));
    impl_->CloseWithoutCommand();
}

void ProgrammerClient::EnterBootloader() {
    static_cast<void>(impl_->Transact(PLP_CMD_FW_ENTER_BOOTLOADER, {}, false));
    impl_->CloseWithoutCommand();
}

TargetSignature ProgrammerClient::ReadTargetSignature(
    const std::array<std::uint8_t, 16>& profile_uuid) {
    std::vector<std::uint8_t> request(20, std::uint8_t{0});
    std::copy(profile_uuid.begin(), profile_uuid.end(), request.begin());
    const auto response = impl_->Transact(PLP_CMD_TARGET_READ_SIGNATURE, request);
    if (response.size() < 36) {
        throw ProgrammerException(PLP_ERR_BAD_LENGTH, "target signature response is truncated");
    }
    TargetSignature signature;
    std::copy_n(response.begin(), 16, signature.matched_profile_uuid.begin());
    std::copy_n(response.begin() + 16, 16, signature.signature_digest.begin());
    signature.raw_signature_length = Rpp_ReadLe16(response.data() + 32);
    signature.flags = Rpp_ReadLe16(response.data() + 34);
    return signature;
}

void ProgrammerClient::DownloadPackage(const OfflineImagePackage& package,
                                       const ProgressCallback& progress) {
    if (package.bytes.empty()) {
        throw ProgrammerException(PLP_ERR_SDK_ARGUMENT, "offline package is empty");
    }
    std::vector<std::uint8_t> begin(48, std::uint8_t{0});
    std::copy(package.package_uuid.begin(), package.package_uuid.end(), begin.begin());
    Rpp_WriteLe32(begin.data() + 16, static_cast<std::uint32_t>(package.bytes.size()));
    Rpp_WriteLe32(begin.data() + 20, package.crc32);
    begin[24] = RPP_OIP_VERSION_MAJOR;
    begin[25] = RPP_OIP_VERSION_MINOR;
    begin[26] = 0xFFu;
    std::copy_n(package.sha256.begin(), 16, begin.begin() + 28);
    auto response = impl_->Transact(PLP_CMD_IMAGE_BEGIN, begin);
    if (response.size() < 26) {
        throw ProgrammerException(PLP_ERR_BAD_LENGTH, "IMAGE_BEGIN response is truncated");
    }
    const std::uint16_t transfer_id = Rpp_ReadLe16(response.data());
    std::uint32_t offset = Rpp_ReadLe32(response.data() + 2);
    const std::size_t maximum = (std::min<std::size_t>)(response[6], PLP_IMAGE_CHUNK_DATA_SIZE);
    if (maximum == 0 || offset > package.bytes.size()) {
        throw ProgrammerException(PLP_ERR_STORAGE_OFFSET, "programmer returned invalid transfer offset/limit");
    }
    while (offset < package.bytes.size()) {
        const auto count = (std::min)(maximum, package.bytes.size() - offset);
        std::vector<std::uint8_t> chunk(7 + count, std::uint8_t{0});
        Rpp_WriteLe16(chunk.data(), transfer_id);
        Rpp_WriteLe32(chunk.data() + 2, offset);
        chunk[6] = static_cast<std::uint8_t>(count);
        std::copy_n(package.bytes.begin() + offset, count, chunk.begin() + 7);
        response = impl_->Transact(PLP_CMD_IMAGE_WRITE_CHUNK, chunk);
        if (response.size() < 6 || Rpp_ReadLe16(response.data()) != transfer_id) {
            throw ProgrammerException(PLP_ERR_BAD_LENGTH, "IMAGE_WRITE_CHUNK response is invalid");
        }
        const auto next = Rpp_ReadLe32(response.data() + 2);
        if (next != offset + count) {
            throw ProgrammerException(PLP_ERR_STORAGE_OFFSET, "programmer did not accept expected chunk range");
        }
        offset = next;
        if (progress) {
            progress(static_cast<std::uint16_t>((static_cast<std::uint64_t>(offset) * 10000u) /
                                                package.bytes.size()),
                     L"Downloading offline image");
        }
    }
    std::vector<std::uint8_t> finalize(42, std::uint8_t{0});
    Rpp_WriteLe16(finalize.data(), transfer_id);
    Rpp_WriteLe32(finalize.data() + 2, static_cast<std::uint32_t>(package.bytes.size()));
    std::copy(package.sha256.begin(), package.sha256.end(), finalize.begin() + 6);
    Rpp_WriteLe32(finalize.data() + 38, package.crc32);
    static_cast<void>(impl_->Transact(PLP_CMD_IMAGE_FINALIZE, finalize));
    if (progress) progress(10000, L"Offline image validated");
}

void ProgrammerClient::ActivatePackage(const std::array<std::uint8_t, 16>& package_uuid,
                                       std::uint32_t expected_generation) {
    std::vector<std::uint8_t> request(40, std::uint8_t{0});
    std::copy(package_uuid.begin(), package_uuid.end(), request.begin());
    Rpp_WriteLe32(request.data() + 16, expected_generation);
    static_cast<void>(impl_->Transact(PLP_CMD_IMAGE_ACTIVATE, request));
}

std::array<std::uint8_t, 16> ProgrammerClient::StartProgram(
    std::uint8_t source_kind,
    const std::array<std::uint8_t, 16>& source_uuid,
    const std::array<std::uint8_t, 16>& profile_uuid,
    std::uint16_t program_flags) {
    const auto job_uuid = NewOperationUuid(3);
    std::vector<std::uint8_t> request(52, std::uint8_t{0});
    std::copy(job_uuid.begin(), job_uuid.end(), request.begin());
    request[16] = source_kind;
    Rpp_WriteLe16(request.data() + 18, program_flags);
    std::copy(source_uuid.begin(), source_uuid.end(), request.begin() + 20);
    std::copy(profile_uuid.begin(), profile_uuid.end(), request.begin() + 36);
    const auto response = impl_->Transact(PLP_CMD_PROGRAM_START, request);
    if (response.size() < 16 || !std::equal(job_uuid.begin(), job_uuid.end(), response.begin())) {
        throw ProgrammerException(PLP_ERR_PROGRAM_JOB_CONFLICT, "PROGRAM_START returned a different job UUID");
    }
    return job_uuid;
}

JobStatus ProgrammerClient::QueryJob(const std::array<std::uint8_t, 16>& job_uuid) {
    const auto response = impl_->Transact(PLP_CMD_PROGRAM_GET_JOB,
                                          std::vector<std::uint8_t>(job_uuid.begin(), job_uuid.end()));
    if (response.size() < 42) {
        throw ProgrammerException(PLP_ERR_BAD_LENGTH, "PROGRAM_GET_JOB response is truncated");
    }
    JobStatus status;
    std::copy_n(response.begin(), 16, status.job_uuid.begin());
    status.operation_state = response[16];
    status.stage = response[17];
    status.terminal_result = response[18];
    status.cancel_state = response[19];
    status.progress_basis_points = Rpp_ReadLe16(response.data() + 20);
    status.attempt = Rpp_ReadLe16(response.data() + 22);
    status.current = Rpp_ReadLe32(response.data() + 24);
    status.total = Rpp_ReadLe32(response.data() + 28);
    status.elapsed_ms = Rpp_ReadLe32(response.data() + 32);
    status.error = Rpp_ReadLe32(response.data() + 36);
    status.detail = Rpp_ReadLe16(response.data() + 40);
    return status;
}

std::uint8_t ProgrammerClient::CancelJob(const std::array<std::uint8_t, 16>& job_uuid,
                                         std::uint16_t reason) {
    std::vector<std::uint8_t> request(36, std::uint8_t{0});
    std::copy(job_uuid.begin(), job_uuid.end(), request.begin());
    Rpp_WriteLe16(request.data() + 16, reason);
    const auto response = impl_->Transact(PLP_CMD_PROGRAM_CANCEL, request);
    if (response.size() < 17) {
        throw ProgrammerException(PLP_ERR_BAD_LENGTH, "PROGRAM_CANCEL response is truncated");
    }
    return response[16];
}

std::wstring FormatUuid(const std::array<std::uint8_t, 16>& uuid) {
    std::wostringstream out;
    out << std::hex << std::setfill(L'0');
    for (std::size_t i = 0; i < uuid.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out << L'-';
        out << std::setw(2) << static_cast<unsigned>(uuid[i]);
    }
    return out.str();
}

std::wstring StatusName(std::uint8_t state) {
    switch (state) {
    case PLP_STATE_BOOTING: return L"Booting";
    case PLP_STATE_READY_UNNEGOTIATED: return L"Ready (not negotiated)";
    case PLP_STATE_READY: return L"Ready";
    case PLP_STATE_SESSION: return L"Session";
    case PLP_STATE_RECEIVING_IMAGE: return L"Receiving image";
    case PLP_STATE_VALIDATING_IMAGE: return L"Validating image";
    case PLP_STATE_PROGRAMMING: return L"Programming";
    case PLP_STATE_RECOVERING: return L"Recovering";
    case PLP_STATE_UPDATING: return L"Updating";
    case PLP_STATE_STORAGE_RECOVERY: return L"Storage recovery";
    case PLP_STATE_ERROR_LATCHED: return L"Error latched";
    default: return L"Unknown";
    }
}

std::wstring StageName(std::uint8_t stage) {
    switch (stage) {
    case 0: return L"NONE";
    case 1: return L"PREFLIGHT";
    case 2: return L"IMAGE_STAGING";
    case 3: return L"PROVIDER_OPEN";
    case 4: return L"TARGET_CONNECT";
    case 5: return L"TARGET_IDENTIFY";
    case 6: return L"PLAN";
    case 7: return L"ERASE";
    case 8: return L"PROGRAM";
    case 9: return L"VERIFY";
    case 10: return L"SENSITIVE_FINALIZE";
    case 11: return L"TARGET_RESET";
    case 12: return L"DISCONNECT_RECOVERY";
    case 13: return L"RESULT_COMMIT";
    case 14: return L"COMPLETE";
    default: return stage >= 128 ? L"DRIVER_STAGE" : L"RESERVED_STAGE";
    }
}

std::wstring TerminalResultName(std::uint8_t result) {
    switch (result) {
    case PLP_TERMINAL_NONE: return L"NONE";
    case PLP_TERMINAL_PASS: return L"PASS";
    case PLP_TERMINAL_FAIL: return L"FAIL";
    case PLP_TERMINAL_CANCELLED: return L"CANCELLED";
    case PLP_TERMINAL_INTERRUPTED: return L"INTERRUPTED";
    case PLP_TERMINAL_OUTCOME_UNKNOWN: return L"OUTCOME_UNKNOWN";
    default: return L"UNKNOWN_RESULT";
    }
}

std::wstring ErrorName(std::uint32_t status) {
    const wchar_t* name = L"UNKNOWN_STATUS";
    switch (status) {
    case PLP_OK: name = L"OK"; break;
    case PLP_ERR_SYSTEM_INTERNAL: name = L"SYSTEM_INTERNAL_INVARIANT"; break;
    case PLP_ERR_SYSTEM_RESOURCE: name = L"SYSTEM_RESOURCE_EXHAUSTED"; break;
    case PLP_ERR_SYSTEM_WATCHDOG: name = L"SYSTEM_WATCHDOG_RESET"; break;
    case PLP_ERR_USB_NOT_FOUND: name = L"USB_DEVICE_NOT_FOUND"; break;
    case PLP_ERR_USB_ACCESS: name = L"USB_ACCESS_DENIED"; break;
    case PLP_ERR_USB_REMOVED: name = L"USB_DEVICE_REMOVED"; break;
    case PLP_ERR_USB_TIMEOUT: name = L"USB_IO_TIMEOUT"; break;
    case PLP_ERR_BAD_MAGIC: name = L"PROTOCOL_BAD_MAGIC"; break;
    case PLP_ERR_BAD_LENGTH: name = L"PROTOCOL_BAD_LENGTH"; break;
    case PLP_ERR_CRC: name = L"PROTOCOL_CRC_MISMATCH"; break;
    case PLP_ERR_BAD_FLAGS: name = L"PROTOCOL_INVALID_FLAGS"; break;
    case PLP_ERR_SEQUENCE_CONFLICT: name = L"PROTOCOL_SEQUENCE_CONFLICT"; break;
    case PLP_ERR_VERSION: name = L"PROTOCOL_VERSION_INCOMPATIBLE"; break;
    case PLP_ERR_COMMAND_UNSUPPORTED: name = L"PROTOCOL_COMMAND_UNSUPPORTED"; break;
    case PLP_ERR_CAPABILITY_UNSUPPORTED: name = L"PROTOCOL_CAPABILITY_UNSUPPORTED"; break;
    case PLP_ERR_INVALID_STATE: name = L"PROTOCOL_INVALID_STATE"; break;
    case PLP_ERR_CURSOR_STALE: name = L"PROTOCOL_CURSOR_STALE"; break;
    case PLP_ERR_UNKNOWN_REMOTE_DETAIL: name = L"UNKNOWN_REMOTE_DETAIL"; break;
    case PLP_ERR_IMAGE_FORMAT: name = L"IMAGE_FORMAT_INVALID"; break;
    case PLP_ERR_IMAGE_CHECKSUM: name = L"IMAGE_CHECKSUM_INVALID"; break;
    case PLP_ERR_IMAGE_ADDRESS_OVERFLOW: name = L"IMAGE_ADDRESS_OVERFLOW"; break;
    case PLP_ERR_IMAGE_ADDRESS: name = L"IMAGE_ADDRESS_UNSUPPORTED"; break;
    case PLP_ERR_IMAGE_OVERLAP: name = L"IMAGE_OVERLAP_CONFLICT"; break;
    case PLP_ERR_IMAGE_DUPLICATE: name = L"IMAGE_DUPLICATE_RECORD"; break;
    case PLP_ERR_IMAGE_OPTION_POLICY: name = L"IMAGE_OPTION_POLICY"; break;
    case PLP_ERR_IMAGE_INCOMPATIBLE: name = L"IMAGE_DEVICE_INCOMPATIBLE"; break;
    case PLP_ERR_INTEGRITY_CRC: name = L"INTEGRITY_CRC_MISMATCH"; break;
    case PLP_ERR_INTEGRITY_HASH: name = L"INTEGRITY_HASH_MISMATCH"; break;
    case PLP_ERR_SIGNATURE_REQUIRED: name = L"SECURITY_SIGNATURE_REQUIRED"; break;
    case PLP_ERR_SIGNATURE_INVALID: name = L"SECURITY_SIGNATURE_INVALID"; break;
    case PLP_ERR_UNAUTHORIZED: name = L"SECURITY_UNAUTHORIZED"; break;
    case PLP_ERR_ROLLBACK: name = L"SECURITY_ROLLBACK_BLOCKED"; break;
    case PLP_ERR_STORAGE_CAPACITY: name = L"STORAGE_CAPACITY_INSUFFICIENT"; break;
    case PLP_ERR_STORAGE_OFFSET: name = L"STORAGE_OFFSET_MISMATCH"; break;
    case PLP_ERR_STORAGE_WRITE: name = L"STORAGE_WRITE_FAILED"; break;
    case PLP_ERR_STORAGE_JOURNAL: name = L"STORAGE_JOURNAL_CORRUPT"; break;
    case PLP_ERR_STORAGE_NO_IMAGE: name = L"STORAGE_NO_VALID_ACTIVE_IMAGE"; break;
    case PLP_ERR_PROGRAM_BUSY: name = L"PROGRAM_BUSY"; break;
    case PLP_ERR_PROGRAM_JOB_CONFLICT: name = L"PROGRAM_JOB_CONFLICT"; break;
    case PLP_WARN_CANCEL_PENDING: name = L"PROGRAM_CANCEL_PENDING"; break;
    case PLP_ERR_VERIFY_MISMATCH: name = L"PROGRAM_VERIFY_MISMATCH"; break;
    case PLP_ERR_OUTCOME_UNKNOWN: name = L"PROGRAM_OUTCOME_UNKNOWN"; break;
    case PLP_ERR_DRIVER_PROFILE: name = L"DRIVER_PROFILE_NOT_FOUND"; break;
    case PLP_ERR_DRIVER_TARGET_MISMATCH: name = L"DRIVER_TARGET_MISMATCH"; break;
    case PLP_ERR_DRIVER_TARGET_STATUS: name = L"DRIVER_TARGET_STATUS"; break;
    case PLP_ERR_TARGET_TIMEOUT: name = L"TARGET_LINK_TIMEOUT"; break;
    case PLP_ERR_TARGET_FRAMING: name = L"TARGET_LINK_UART_FRAMING"; break;
    case PLP_ERR_TARGET_DIRECTION: name = L"TARGET_LINK_DIRECTION_COLLISION"; break;
    case PLP_ERR_OFFLINE_NOT_ACTIVE: name = L"OFFLINE_IMAGE_NOT_ACTIVE"; break;
    case PLP_ERR_OFFLINE_TRIGGER: name = L"OFFLINE_TRIGGER_REJECTED"; break;
    case PLP_ERR_CONFIG_SCHEMA: name = L"CONFIG_SCHEMA_UNSUPPORTED"; break;
    case PLP_ERR_CONFIG_VALIDATION: name = L"CONFIG_VALIDATION_FAILED"; break;
    case PLP_ERR_CONFIG_PERSIST: name = L"CONFIG_PERSIST_FAILED"; break;
    case PLP_ERR_UPDATE_INCOMPATIBLE: name = L"UPDATE_PACKAGE_INCOMPATIBLE"; break;
    case PLP_ERR_UPDATE_APPLY: name = L"UPDATE_APPLY_FAILED"; break;
    case PLP_ERR_BARCODE: name = L"PRODUCTION_BARCODE_INVALID"; break;
    case PLP_ERR_HISTORY_COMMIT: name = L"PRODUCTION_HISTORY_COMMIT_FAILED"; break;
    case PLP_ERR_MES: name = L"PRODUCTION_MES_UNAVAILABLE"; break;
    case PLP_ERR_PLUGIN_ABI: name = L"PLUGIN_ABI_INCOMPATIBLE"; break;
    case PLP_ERR_PLUGIN_TRUST: name = L"PLUGIN_TRUST_REJECTED"; break;
    case PLP_ERR_SDK_ARGUMENT: name = L"SDK_INVALID_ARGUMENT"; break;
    case PLP_ERR_SDK_HANDLE: name = L"SDK_INVALID_HANDLE"; break;
    case PLP_ERR_SDK_BUSY: name = L"SDK_OPERATION_BUSY"; break;
    case PLP_ERR_FILE_PERMISSION: name = L"FILE_PERMISSION_DENIED"; break;
    case PLP_ERR_FILE_DISK_FULL: name = L"FILE_DISK_FULL"; break;
    case PLP_ERR_UART_NOT_FOUND: name = L"UART_PORT_NOT_FOUND"; break;
    case PLP_ERR_UART_ACCESS: name = L"UART_ACCESS_DENIED"; break;
    case PLP_ERR_UART_REMOVED: name = L"UART_PORT_REMOVED"; break;
    case PLP_ERR_UART_TIMEOUT: name = L"UART_IO_TIMEOUT"; break;
    case PLP_ERR_UNKNOWN: name = L"UNKNOWN_UNCLASSIFIED"; break;
    default: break;
    }
    std::wostringstream out;
    out << name << L" (0x" << std::uppercase << std::hex << std::setw(8)
        << std::setfill(L'0') << status << L")";
    return out.str();
}

} // namespace rl78_programmer::sdk

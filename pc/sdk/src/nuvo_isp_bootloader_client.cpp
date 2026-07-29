#include "nuvo_isp_bootloader_client.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>

#include "rl78_programmer/plp_contract.h"
#include "hid_transport.h"
#include "serial_transport.h"

namespace rl78_programmer::sdk::detail {
namespace {

constexpr int kConnectAttemptTimeoutMs = 300;
constexpr int kReadPollTimeoutMs = 200;

} // namespace

void NuvoIspBootloaderClient::Connect(const DeviceDescriptor& descriptor,
                                      int timeout_ms) {
    Disconnect();
    if (descriptor.transport == TransportKind::Uart) {
        auto transport = std::make_unique<SerialTransport>();
        transport->Open(descriptor, timeout_ms);
        transport_ = std::move(transport);
    } else {
        auto transport = std::make_unique<HidTransport>();
        transport->Open(descriptor, timeout_ms);
        transport_ = std::move(transport);
    }
    packet_index_ = 18u;
}

void NuvoIspBootloaderClient::Disconnect() noexcept {
    if (transport_ != nullptr) {
        transport_->Close();
        transport_.reset();
    }
    packet_index_ = 18u;
}

bool NuvoIspBootloaderClient::IsConnected() const noexcept {
    return transport_ != nullptr && transport_->IsOpen();
}

DeviceDescriptor NuvoIspBootloaderClient::CurrentDevice() const {
    return transport_ != nullptr ? transport_->CurrentDevice()
                                 : DeviceDescriptor{};
}

BridgeBootloaderInfo NuvoIspBootloaderClient::Probe(
    const BridgeFirmwareCancelCallback& cancel,
    int handshake_timeout_ms) {
    BridgeBootloaderInfo info;
    info.descriptor = CurrentDevice();
    info.application_geometry =
        ConnectTarget(cancel, handshake_timeout_ms);

    TransactionResult response =
        Transact(nuvo_isp::kCommandGetFirmwareVersion, nullptr, 0u, true,
                 normal_response_timeout_ms_, true, cancel);
    info.firmware_version = response.response[8];

    response = Transact(nuvo_isp::kCommandGetDeviceId, nullptr, 0u, true,
                        normal_response_timeout_ms_, true, cancel);
    info.device_id = nuvo_isp::ReadLe32(response.response.data() + 8u);

    info.config = ReadConfiguration(cancel);
    return info;
}

void NuvoIspBootloaderClient::ProgramApplication(
    const BridgeFirmwareImage& image,
    const BridgeFirmwareProgressCallback& progress,
    const BridgeFirmwareCancelCallback& cancel) {
    if (!IsConnected()) {
        throw ProgrammerException(PLP_ERR_SDK_HANDLE,
                                  "bootloader transport is not connected");
    }
    if (image.bytes.empty() || image.bytes.size() >
                                   static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)())) {
        throw ProgrammerException(PLP_ERR_SDK_ARGUMENT,
                                  "bridge application image size is invalid");
    }

    const BridgeApplicationGeometry application_geometry = ConnectTarget(cancel);
    BridgeFirmwareUpdater::ValidateCompatibility(image.profile,
                                                 application_geometry);
    const std::array<std::uint32_t, 4> config = ReadConfiguration(cancel);
    BridgeFirmwareUpdater::ValidateIapConfiguration(image.profile.target,
                                                    config[0]);
    SyncPacketNumber(cancel);

    const std::uint32_t total = static_cast<std::uint32_t>(image.bytes.size());
    std::uint32_t offset = 0;
    std::uint32_t packet_count = 0;
    TransactionResult final_response;

    while (offset < total) {
        ThrowIfCancelled(cancel);

        std::array<std::uint8_t, nuvo_isp::kPayloadSize> payload{};
        std::size_t prefix_size = 0;
        std::size_t write_size = 0;
        std::uint32_t command = 0;
        int response_timeout_ms = normal_response_timeout_ms_;

        if (offset == 0u) {
            command = nuvo_isp::kCommandUpdateAprom;
            prefix_size = 8u;
            nuvo_isp::WriteLe32(payload.data(), image.profile.application_start);
            nuvo_isp::WriteLe32(payload.data() + 4u, total);
            write_size = (std::min)(image.bytes.size(),
                                    nuvo_isp::kPayloadSize - prefix_size);
            response_timeout_ms = long_response_timeout_ms_;
        } else {
            write_size = (std::min)(image.bytes.size() - offset,
                                    nuvo_isp::kPayloadSize);
        }

        std::copy_n(image.bytes.data() + offset, write_size,
                    payload.data() + prefix_size);
        if (offset + static_cast<std::uint32_t>(write_size) == total) {
            response_timeout_ms = long_response_timeout_ms_;
        }

        bool accepted = false;
        for (int attempt = 0; attempt < program_retry_count_ && !accepted; ++attempt) {
            final_response = Transact(command, payload.data(), prefix_size + write_size,
                                      true, response_timeout_ms, false, cancel);
            if (final_response.validation.checksum_ok &&
                final_response.validation.index_ok) {
                accepted = true;
                break;
            }
            if (offset == 0u || attempt + 1 >= program_retry_count_) {
                throw ProgrammerException(PLP_ERR_VERIFY_MISMATCH,
                                          ResponseError(final_response.validation));
            }
            SendResend(cancel);
        }

        offset += static_cast<std::uint32_t>(write_size);
        ++packet_count;
        if (progress) {
            BridgeFirmwareUpdateProgress update;
            update.bytes_done = offset;
            update.total_bytes = total;
            update.packet_index = packet_count;
            update.basis_points = static_cast<std::uint16_t>(
                (static_cast<std::uint64_t>(offset) * 10000u) / total);
            progress(update);
        }
    }

    const std::uint16_t target_checksum =
        nuvo_isp::ReadLe16(final_response.response.data() + 8u);
    if (target_checksum != image.additive_checksum) {
        throw ProgrammerException(
            PLP_ERR_VERIFY_MISMATCH,
            "bootloader whole-image additive checksum does not match the selected binary");
    }
}

void NuvoIspBootloaderClient::RunApplication() {
    SendNoResponse(nuvo_isp::kCommandRunAprom);
}

void NuvoIspBootloaderClient::ResetBootloader() {
    SendNoResponse(nuvo_isp::kCommandReset);
}

BridgeApplicationGeometry NuvoIspBootloaderClient::ConnectTarget(
    const BridgeFirmwareCancelCallback& cancel,
    int handshake_timeout_ms) {
    if (!IsConnected()) {
        throw ProgrammerException(PLP_ERR_SDK_HANDLE,
                                  "bootloader transport is not connected");
    }

    const int bounded_handshake_timeout =
        handshake_timeout_ms > 0
            ? (std::max)(handshake_timeout_ms, kConnectAttemptTimeoutMs)
            : long_response_timeout_ms_;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(
                              bounded_handshake_timeout);
    std::string last_error = "bootloader did not respond";
    while (std::chrono::steady_clock::now() < deadline) {
        ThrowIfCancelled(cancel);
        try {
            const TransactionResult response =
                Transact(nuvo_isp::kCommandConnect, nullptr, 0u, false,
                         kConnectAttemptTimeoutMs, true, cancel);
            BridgeApplicationGeometry application_geometry;
            application_geometry.application_size =
                nuvo_isp::ReadLe32(response.response.data() + 8u);
            application_geometry.application_start =
                nuvo_isp::ReadLe32(response.response.data() + 12u);
            application_geometry.application_end =
                nuvo_isp::ReadLe32(response.response.data() + 16u);
            application_geometry.crc32_address =
                nuvo_isp::ReadLe32(response.response.data() + 20u);
            application_geometry.board_id =
                nuvo_isp::ReadLe32(response.response.data() + 24u);
            SyncPacketNumber(cancel);
            return application_geometry;
        } catch (const ProgrammerException& error) {
            if (error.status() != PLP_ERR_USB_TIMEOUT &&
                error.status() != PLP_ERR_UART_TIMEOUT &&
                error.status() != PLP_ERR_VERIFY_MISMATCH) {
                throw;
            }
            last_error = error.what();
            const std::string suffix =
                " [" + std::to_string(error.status()) + "]";
            if (last_error.size() >= suffix.size() &&
                last_error.compare(last_error.size() - suffix.size(),
                                   suffix.size(), suffix) == 0) {
                last_error.erase(last_error.size() - suffix.size());
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    throw ProgrammerException(
        CurrentDevice().transport == TransportKind::Uart
            ? PLP_ERR_UART_TIMEOUT
            : PLP_ERR_USB_TIMEOUT,
                              "NuvoISP bootloader handshake timeout: " + last_error);
}

std::array<std::uint32_t, 4> NuvoIspBootloaderClient::ReadConfiguration(
    const BridgeFirmwareCancelCallback& cancel) {
    const TransactionResult response =
        Transact(nuvo_isp::kCommandReadConfig, nullptr, 0u, true,
                 normal_response_timeout_ms_, true, cancel);
    std::array<std::uint32_t, 4> config{};
    for (std::size_t index = 0; index < config.size(); ++index) {
        config[index] =
            nuvo_isp::ReadLe32(response.response.data() + 8u + index * 4u);
    }
    return config;
}

void NuvoIspBootloaderClient::SyncPacketNumber(
    const BridgeFirmwareCancelCallback& cancel) {
    std::array<std::uint8_t, 4> payload{};
    packet_index_ = 1u;
    nuvo_isp::WriteLe32(payload.data(), packet_index_);
    static_cast<void>(Transact(nuvo_isp::kCommandSyncPacketNumber,
                               payload.data(), payload.size(), false,
                               normal_response_timeout_ms_, true, cancel));
}

NuvoIspBootloaderClient::TransactionResult NuvoIspBootloaderClient::Transact(
    std::uint32_t command,
    const std::uint8_t* payload,
    std::size_t payload_size,
    bool check_index,
    int response_timeout_ms,
    bool require_valid,
    const BridgeFirmwareCancelCallback& cancel) {
    if (!IsConnected()) {
        throw ProgrammerException(PLP_ERR_SDK_HANDLE,
                                  "bootloader transport is not connected");
    }
    ThrowIfCancelled(cancel);

    const std::uint32_t sent_index = packet_index_;
    const nuvo_isp::Packet request =
        nuvo_isp::BuildPacket(command, sent_index, payload, payload_size);
    transport_->WriteRaw(request);
    packet_index_ += 2u;

    TransactionResult result;
    result.response = ReadResponse(request, sent_index + 1u, check_index,
                                   response_timeout_ms, cancel);
    result.validation =
        nuvo_isp::ValidateResponse(request, result.response, sent_index + 1u,
                                   check_index);
    if (require_valid &&
        (!result.validation.checksum_ok || !result.validation.index_ok)) {
        throw ProgrammerException(PLP_ERR_VERIFY_MISMATCH,
                                  ResponseError(result.validation));
    }
    return result;
}

nuvo_isp::Packet NuvoIspBootloaderClient::ReadResponse(
    const nuvo_isp::Packet& request,
    std::uint32_t expected_index,
    bool check_index,
    int timeout_ms,
    const BridgeFirmwareCancelCallback& cancel) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds((std::max)(timeout_ms, 100));
    const bool filter_stale_reports =
        CurrentDevice().product_id == BridgeFirmwareUpdater::kLegacyBootloaderProductId;
    while (std::chrono::steady_clock::now() < deadline) {
        ThrowIfCancelled(cancel);
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        const int slice = (std::min)(kReadPollTimeoutMs,
                                     (std::max)(1, static_cast<int>(remaining.count())));
        try {
            const nuvo_isp::Packet response = transport_->ReadRaw(slice);
            const nuvo_isp::ResponseValidation validation =
                nuvo_isp::ValidateResponse(request, response, expected_index,
                                           check_index);
            if (!filter_stale_reports ||
                (validation.checksum_ok && validation.index_ok)) {
                return response;
            }
        } catch (const ProgrammerException& error) {
            if (error.status() != PLP_ERR_USB_TIMEOUT &&
                error.status() != PLP_ERR_UART_TIMEOUT) {
                throw;
            }
        }
    }
    throw ProgrammerException(
        CurrentDevice().transport == TransportKind::Uart
            ? PLP_ERR_UART_TIMEOUT
            : PLP_ERR_USB_TIMEOUT,
                              "NuvoISP bootloader response timeout");
}

void NuvoIspBootloaderClient::SendResend(
    const BridgeFirmwareCancelCallback& cancel) {
    static_cast<void>(Transact(nuvo_isp::kCommandResendPacket, nullptr, 0u,
                               false, long_response_timeout_ms_, true, cancel));
}

void NuvoIspBootloaderClient::SendNoResponse(std::uint32_t command) {
    if (!IsConnected()) {
        throw ProgrammerException(PLP_ERR_SDK_HANDLE,
                                  "bootloader transport is not connected");
    }
    const nuvo_isp::Packet request =
        nuvo_isp::BuildPacket(command, packet_index_, nullptr, 0u);
    transport_->WriteRaw(request);
    packet_index_ += 2u;
}

void NuvoIspBootloaderClient::ThrowIfCancelled(
    const BridgeFirmwareCancelCallback& cancel) {
    if (cancel && cancel()) {
        throw ProgrammerException(PLP_ERR_SDK_BUSY,
                                  "bridge firmware update cancelled by caller");
    }
}

std::string NuvoIspBootloaderClient::ResponseError(
    const nuvo_isp::ResponseValidation& validation) {
    std::ostringstream output;
    output << "NuvoISP response validation failed";
    if (!validation.checksum_ok) {
        output << ": checksum expected=0x" << std::uppercase << std::hex
               << std::setw(4) << std::setfill('0') << validation.expected_checksum
               << " actual=0x" << std::setw(4) << validation.response_checksum;
    }
    if (!validation.index_ok) {
        output << ": packet index expected=" << std::dec
               << validation.expected_index << " actual="
               << validation.response_index;
    }
    return output.str();
}

} // namespace rl78_programmer::sdk::detail

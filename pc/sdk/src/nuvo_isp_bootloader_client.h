#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "bridge_firmware_updater.h"
#include "programmer_transport.h"
#include "nuvo_isp_protocol.h"

namespace rl78_programmer::sdk::detail {

class NuvoIspBootloaderClient {
public:
    NuvoIspBootloaderClient() = default;
    ~NuvoIspBootloaderClient() = default;

    NuvoIspBootloaderClient(const NuvoIspBootloaderClient&) = delete;
    NuvoIspBootloaderClient& operator=(const NuvoIspBootloaderClient&) = delete;

    void Connect(const DeviceDescriptor& descriptor, int timeout_ms);
    void Disconnect() noexcept;
    [[nodiscard]] bool IsConnected() const noexcept;
    [[nodiscard]] DeviceDescriptor CurrentDevice() const;

    BridgeBootloaderInfo Probe(const BridgeFirmwareCancelCallback& cancel,
                               int handshake_timeout_ms = 0);
    void ProgramApplication(const BridgeFirmwareImage& image,
                            const BridgeFirmwareProgressCallback& progress,
                            const BridgeFirmwareCancelCallback& cancel);
    void RunApplication();
    void ResetBootloader();

private:
    struct TransactionResult {
        nuvo_isp::Packet response{};
        nuvo_isp::ResponseValidation validation{};
    };

    BridgeApplicationGeometry ConnectTarget(
        const BridgeFirmwareCancelCallback& cancel,
        int handshake_timeout_ms = 0);
    std::array<std::uint32_t, 4> ReadConfiguration(
        const BridgeFirmwareCancelCallback& cancel);
    void SyncPacketNumber(const BridgeFirmwareCancelCallback& cancel);
    TransactionResult Transact(std::uint32_t command,
                               const std::uint8_t* payload,
                               std::size_t payload_size,
                               bool check_index,
                               int response_timeout_ms,
                               bool require_valid,
                               const BridgeFirmwareCancelCallback& cancel);
    nuvo_isp::Packet ReadResponse(const nuvo_isp::Packet& request,
                                  std::uint32_t expected_index,
                                  bool check_index,
                                  int timeout_ms,
                                  const BridgeFirmwareCancelCallback& cancel);
    void SendResend(const BridgeFirmwareCancelCallback& cancel);
    void SendNoResponse(std::uint32_t command);
    static void ThrowIfCancelled(const BridgeFirmwareCancelCallback& cancel);
    static std::string ResponseError(const nuvo_isp::ResponseValidation& validation);

    std::unique_ptr<ProgrammerTransport> transport_;
    int normal_response_timeout_ms_ = 5000;
    int long_response_timeout_ms_ = 25000;
    int program_retry_count_ = 10;
    std::uint32_t packet_index_ = 18u;
};

} // namespace rl78_programmer::sdk::detail

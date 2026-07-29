#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace rl78_programmer::sdk::detail::nuvo_isp {

constexpr std::size_t kPacketSize = 64u;
constexpr std::size_t kHeaderSize = 8u;
constexpr std::size_t kPayloadSize = kPacketSize - kHeaderSize;

constexpr std::uint32_t kCommandUpdateAprom = 0x000000A0u;
constexpr std::uint32_t kCommandReadConfig = 0x000000A2u;
constexpr std::uint32_t kCommandSyncPacketNumber = 0x000000A4u;
constexpr std::uint32_t kCommandGetFirmwareVersion = 0x000000A6u;
constexpr std::uint32_t kCommandRunAprom = 0x000000ABu;
constexpr std::uint32_t kCommandReset = 0x000000ADu;
constexpr std::uint32_t kCommandConnect = 0x000000AEu;
constexpr std::uint32_t kCommandGetDeviceId = 0x000000B1u;
constexpr std::uint32_t kCommandResendPacket = 0x000000FFu;

using Packet = std::array<std::uint8_t, kPacketSize>;

struct ResponseValidation {
    std::uint16_t expected_checksum = 0;
    std::uint16_t response_checksum = 0;
    std::uint32_t expected_index = 0;
    std::uint32_t response_index = 0;
    bool checksum_ok = false;
    bool index_ok = false;
};

Packet BuildPacket(std::uint32_t command,
                   std::uint32_t packet_index,
                   const std::uint8_t* payload,
                   std::size_t payload_size);
std::uint16_t AdditiveChecksum(const std::uint8_t* bytes, std::size_t size);
std::uint16_t ReadLe16(const std::uint8_t* bytes);
std::uint32_t ReadLe32(const std::uint8_t* bytes);
void WriteLe32(std::uint8_t* bytes, std::uint32_t value);
ResponseValidation ValidateResponse(const Packet& request,
                                    const Packet& response,
                                    std::uint32_t expected_index,
                                    bool check_index);

} // namespace rl78_programmer::sdk::detail::nuvo_isp

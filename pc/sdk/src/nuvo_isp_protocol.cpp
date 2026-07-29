#include "nuvo_isp_protocol.h"

#include <algorithm>
#include <stdexcept>

namespace rl78_programmer::sdk::detail::nuvo_isp {

Packet BuildPacket(std::uint32_t command,
                   std::uint32_t packet_index,
                   const std::uint8_t* payload,
                   std::size_t payload_size) {
    if (payload_size > kPayloadSize) {
        throw std::invalid_argument("NuvoISP payload exceeds 56 bytes");
    }
    if (payload_size != 0u && payload == nullptr) {
        throw std::invalid_argument("NuvoISP payload pointer is null");
    }

    Packet packet{};
    WriteLe32(packet.data(), command);
    WriteLe32(packet.data() + 4u, packet_index);
    if (payload_size != 0u) {
        std::copy_n(payload, payload_size, packet.data() + kHeaderSize);
    }
    return packet;
}

std::uint16_t AdditiveChecksum(const std::uint8_t* bytes, std::size_t size) {
    if (size != 0u && bytes == nullptr) {
        throw std::invalid_argument("checksum input pointer is null");
    }
    std::uint32_t sum = 0;
    for (std::size_t index = 0; index < size; ++index) {
        sum += bytes[index];
    }
    return static_cast<std::uint16_t>(sum & 0xFFFFu);
}

std::uint16_t ReadLe16(const std::uint8_t* bytes) {
    return static_cast<std::uint16_t>(bytes[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1]) << 8u);
}

std::uint32_t ReadLe32(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8u) |
           (static_cast<std::uint32_t>(bytes[2]) << 16u) |
           (static_cast<std::uint32_t>(bytes[3]) << 24u);
}

void WriteLe32(std::uint8_t* bytes, std::uint32_t value) {
    bytes[0] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes[1] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
    bytes[2] = static_cast<std::uint8_t>((value >> 16u) & 0xFFu);
    bytes[3] = static_cast<std::uint8_t>((value >> 24u) & 0xFFu);
}

ResponseValidation ValidateResponse(const Packet& request,
                                    const Packet& response,
                                    std::uint32_t expected_index,
                                    bool check_index) {
    ResponseValidation validation;
    validation.expected_checksum = AdditiveChecksum(request.data(), request.size());
    validation.response_checksum = ReadLe16(response.data());
    validation.expected_index = expected_index;
    validation.response_index = ReadLe32(response.data() + 4u);
    validation.checksum_ok = validation.response_checksum == validation.expected_checksum;
    validation.index_ok = !check_index || validation.response_index == expected_index;
    return validation;
}

} // namespace rl78_programmer::sdk::detail::nuvo_isp

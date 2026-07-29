#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "programmer_sdk.h"

namespace rl78_programmer::sdk {

enum class BridgeFirmwareTarget : std::uint8_t {
    M032SE3AE = 1,
    M484SIDAE = 2,
    M032TestBoard = 3
};

struct BridgeFirmwareProfile {
    BridgeFirmwareTarget target = BridgeFirmwareTarget::M032SE3AE;
    std::wstring display_name;
    std::uint32_t application_start = 0;
    std::uint32_t application_size = 0;
    std::uint32_t crc32_offset = 0;
    std::uint32_t expected_board_id = 0;
};

struct BridgeFirmwareImage {
    BridgeFirmwareProfile profile;
    std::vector<std::uint8_t> bytes;
    std::uint32_t stored_crc32 = 0;
    std::uint32_t calculated_crc32 = 0;
    std::uint16_t additive_checksum = 0;
};

struct BridgeApplicationGeometry {
    std::uint32_t application_start = 0;
    std::uint32_t application_size = 0;
    std::uint32_t application_end = 0;
    std::uint32_t crc32_address = 0;
    std::uint32_t board_id = 0;
};

struct BridgeBootloaderInfo {
    DeviceDescriptor descriptor;
    std::uint8_t firmware_version = 0;
    std::uint32_t device_id = 0;
    std::array<std::uint32_t, 4> config{};
    BridgeApplicationGeometry application_geometry;
};

struct BridgeFirmwareUpdateProgress {
    std::uint32_t bytes_done = 0;
    std::uint32_t total_bytes = 0;
    std::uint32_t packet_index = 0;
    std::uint16_t basis_points = 0;
};

using BridgeFirmwareProgressCallback =
    std::function<void(const BridgeFirmwareUpdateProgress& progress)>;
using BridgeFirmwareCancelCallback = std::function<bool()>;

class BridgeFirmwareUpdater {
public:
    static constexpr std::uint16_t kNuvotonVendorId = 0x0416u;
    static constexpr std::uint16_t kBootloaderProductId = 0x3F00u;
    static constexpr std::uint16_t kLegacyBootloaderProductId = 0xA316u;

    BridgeFirmwareUpdater();
    ~BridgeFirmwareUpdater();

    BridgeFirmwareUpdater(const BridgeFirmwareUpdater&) = delete;
    BridgeFirmwareUpdater& operator=(const BridgeFirmwareUpdater&) = delete;

    static BridgeFirmwareProfile Profile(BridgeFirmwareTarget target);
    static void ValidateCompatibility(
        const BridgeFirmwareProfile& profile,
        const BridgeApplicationGeometry& application_geometry);
    static void ValidateIapConfiguration(BridgeFirmwareTarget target,
                                         std::uint32_t config0);
    static BridgeFirmwareImage ValidateImage(BridgeFirmwareTarget target,
                                               const std::vector<std::uint8_t>& bytes);
    static BridgeFirmwareImage LoadApplicationImage(BridgeFirmwareTarget target,
                                                    const std::wstring& path);
    static std::vector<DeviceDescriptor> EnumerateBootloaders();
    static std::vector<DeviceDescriptor> EnumerateSerialPorts();

    void ConnectUnique(int timeout_ms = 2000);
    void Connect(const DeviceDescriptor& descriptor, int timeout_ms = 2000);
    void Disconnect() noexcept;
    [[nodiscard]] bool IsConnected() const noexcept;
    [[nodiscard]] DeviceDescriptor CurrentDevice() const;

    BridgeBootloaderInfo Probe(
        const BridgeFirmwareCancelCallback& cancel = {},
        int handshake_timeout_ms = 0);
    void ProgramApplication(const BridgeFirmwareImage& image,
                            const BridgeFirmwareProgressCallback& progress = {},
                            const BridgeFirmwareCancelCallback& cancel = {});
    void RunApplication();
    void ResetBootloader();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rl78_programmer::sdk

#include "bridge_firmware_updater.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

#include "nuvo_isp_protocol.h"
#include "rl78_programmer/integrity.h"
#include "rl78_programmer/plp_contract.h"

namespace rl78_programmer::sdk {
namespace {

constexpr std::uint32_t kApplicationStart = 0x00004000u;
constexpr std::uint32_t kM032ApplicationSize = 0x0000BC00u;
constexpr std::uint32_t kM484ApplicationSize = 0x0007C000u;
constexpr std::uint32_t kCrc32Size = 4u;
constexpr std::uint32_t kConfig0CbsMask = 0x000000C0u;
constexpr std::uint32_t kConfig0ApromWithIap = 0x00000080u;
constexpr std::uint32_t kConfig0MbsMask = 0x00000020u;
constexpr std::uint32_t kM032TestBoardId = 0x3142544Du;

std::uint32_t ReadLe32(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8u) |
           (static_cast<std::uint32_t>(bytes[2]) << 16u) |
           (static_cast<std::uint32_t>(bytes[3]) << 24u);
}

} // namespace

BridgeFirmwareProfile BridgeFirmwareUpdater::Profile(BridgeFirmwareTarget target) {
    BridgeFirmwareProfile profile;
    profile.target = target;
    profile.application_start = kApplicationStart;

    switch (target) {
    case BridgeFirmwareTarget::M032SE3AE:
        profile.display_name = L"M032SE3AE bridge application";
        profile.application_size = kM032ApplicationSize;
        break;
    case BridgeFirmwareTarget::M484SIDAE:
        profile.display_name = L"M484SIDAE Nu-Link2-Me bridge application";
        profile.application_size = kM484ApplicationSize;
        break;
    case BridgeFirmwareTarget::M032TestBoard:
        profile.display_name = L"M032 TEST BOARD UART application";
        profile.application_size = kM032ApplicationSize;
        profile.expected_board_id = kM032TestBoardId;
        break;
    default:
        throw ProgrammerException(PLP_ERR_UPDATE_INCOMPATIBLE,
                                  "unknown bridge firmware target profile");
    }

    profile.crc32_offset = profile.application_size - kCrc32Size;
    return profile;
}

void BridgeFirmwareUpdater::ValidateCompatibility(
    const BridgeFirmwareProfile& profile,
    const BridgeApplicationGeometry& application_geometry) {
    const std::uint64_t expected_end =
        static_cast<std::uint64_t>(profile.application_start) +
        static_cast<std::uint64_t>(profile.application_size);
    const std::uint64_t expected_crc =
        static_cast<std::uint64_t>(profile.application_start) +
        static_cast<std::uint64_t>(profile.crc32_offset);

    if (profile.application_size == 0u ||
        static_cast<std::uint64_t>(profile.crc32_offset) + kCrc32Size !=
            profile.application_size ||
        expected_end > (std::numeric_limits<std::uint32_t>::max)() ||
        expected_crc > (std::numeric_limits<std::uint32_t>::max)()) {
        throw ProgrammerException(PLP_ERR_UPDATE_INCOMPATIBLE,
                                  "selected bridge firmware profile geometry is invalid");
    }

    if (application_geometry.application_start != profile.application_start ||
        application_geometry.application_size != profile.application_size ||
        application_geometry.application_end !=
            static_cast<std::uint32_t>(expected_end) ||
        application_geometry.crc32_address !=
            static_cast<std::uint32_t>(expected_crc) ||
        (profile.expected_board_id != 0u &&
         application_geometry.board_id != profile.expected_board_id)) {
        std::ostringstream message;
        message << std::hex << std::uppercase << std::setfill('0')
                << "bootloader application geometry does not match the selected target profile "
                << "(expected start=0x" << std::setw(8) << profile.application_start
                << ", size=0x" << std::setw(8) << profile.application_size
                << ", end=0x" << std::setw(8)
                << static_cast<std::uint32_t>(expected_end)
                << ", CRC=0x" << std::setw(8)
                << static_cast<std::uint32_t>(expected_crc)
                << "; reported start=0x" << std::setw(8)
                << application_geometry.application_start
                << ", size=0x" << std::setw(8)
                << application_geometry.application_size
                << ", end=0x" << std::setw(8)
                << application_geometry.application_end
                << ", CRC=0x" << std::setw(8)
                << application_geometry.crc32_address
                << ", board=0x" << std::setw(8)
                << application_geometry.board_id << ')';
        throw ProgrammerException(PLP_ERR_UPDATE_INCOMPATIBLE, message.str());
    }
}

void BridgeFirmwareUpdater::ValidateIapConfiguration(
    BridgeFirmwareTarget target, std::uint32_t config0) {
    if ((config0 & kConfig0CbsMask) != kConfig0ApromWithIap) {
        throw ProgrammerException(
            PLP_ERR_UPDATE_INCOMPATIBLE,
            "bridge CONFIG0 is not configured for APROM with IAP; set "
            "CBS[7:6]=10 with Nu-Link ICP before uploading bridge firmware");
    }

    switch (target) {
    case BridgeFirmwareTarget::M032SE3AE:
    case BridgeFirmwareTarget::M032TestBoard:
        break;
    case BridgeFirmwareTarget::M484SIDAE:
        if ((config0 & kConfig0MbsMask) == 0u) {
            throw ProgrammerException(
                PLP_ERR_UPDATE_INCOMPATIBLE,
                "M484SIDAE CONFIG0 does not permit APROM boot selection; set "
                "MBS[5]=1 and CBS[7:6]=10 with Nu-Link ICP before uploading "
                "bridge firmware");
        }
        break;
    default:
        throw ProgrammerException(PLP_ERR_UPDATE_INCOMPATIBLE,
                                  "unknown bridge firmware target profile");
    }
}

BridgeFirmwareImage BridgeFirmwareUpdater::ValidateImage(
    BridgeFirmwareTarget target,
    const std::vector<std::uint8_t>& bytes) {
    BridgeFirmwareImage image;
    image.profile = Profile(target);

    if (bytes.size() != image.profile.application_size) {
        throw ProgrammerException(
            PLP_ERR_UPDATE_INCOMPATIBLE,
            "bridge application binary size does not match the selected target profile");
    }

    image.stored_crc32 = ReadLe32(bytes.data() + image.profile.crc32_offset);
    image.calculated_crc32 =
        Rpp_Crc32IsoHdlc(bytes.data(), image.profile.crc32_offset);
    if (image.stored_crc32 != image.calculated_crc32) {
        throw ProgrammerException(PLP_ERR_IMAGE_CHECKSUM,
                                  "bridge application CRC32/ISO-HDLC does not match");
    }

    image.additive_checksum =
        detail::nuvo_isp::AdditiveChecksum(bytes.data(), bytes.size());
    image.bytes = bytes;
    return image;
}

BridgeFirmwareImage BridgeFirmwareUpdater::LoadApplicationImage(
    BridgeFirmwareTarget target, const std::wstring& path) {
    if (path.empty()) {
        throw ProgrammerException(PLP_ERR_SDK_ARGUMENT,
                                  "bridge application image path is empty");
    }

    std::ifstream input(std::filesystem::path(path), std::ios::binary | std::ios::ate);
    if (!input) {
        throw ProgrammerException(PLP_ERR_FILE_PERMISSION,
                                  "failed to open bridge application image");
    }

    const std::streampos end = input.tellg();
    if (end < 0 || static_cast<std::uintmax_t>(end) >
                       static_cast<std::uintmax_t>((std::numeric_limits<std::size_t>::max)())) {
        throw ProgrammerException(PLP_ERR_UPDATE_INCOMPATIBLE,
                                  "bridge application image size is invalid");
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    if (!input || input.gcount() != static_cast<std::streamsize>(bytes.size())) {
        throw ProgrammerException(PLP_ERR_FILE_PERMISSION,
                                  "failed to read complete bridge application image");
    }
    return ValidateImage(target, bytes);
}

} // namespace rl78_programmer::sdk

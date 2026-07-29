#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "bridge_firmware_updater.h"
#include "nuvo_isp_protocol.h"
#include "rl78_programmer/integrity.h"
#include "rl78_programmer/intel_hex.h"
#include "rl78_programmer/offline_image.h"
#include "rl78_programmer/oip_format.h"
#include "rl78_programmer/plp_contract.h"
#include "rl78_programmer/plp_frame.h"
#include "rl78_programmer/srecord.h"
#include "programmer_core.h"
#include "m032_programmer.h"
#include "m032_board_io.h"
#include "m032_board_profile.h"
#include "m032_package_store.h"
#include "m032_test_board_profile.h"
#include "nulink2me_board_profile.h"
#include "nulink2me_image_cache.h"
#include "nulink2me_offline_recovery.h"
#include "nulink2me_spim_storage.h"
#include "rl78_protocol_b.h"
#include "session_file_logger.h"
#include "serial_plp_framer.h"
#include "validated_image_source.h"

namespace {

int g_failures = 0;

void Check(bool condition, const char* name) {
    if (!condition) {
        ++g_failures;
        std::cerr << "FAIL: " << name << '\n';
    } else {
        std::cout << "PASS: " << name << '\n';
    }
}

class ScopedTestDirectory final {
public:
    ScopedTestDirectory() {
        static std::atomic<unsigned> sequence{0u};
        path_ = std::filesystem::temp_directory_path() /
                (L"rl78_session_logger_" +
                 std::to_wstring(GetCurrentProcessId()) + L"_" +
                 std::to_wstring(GetTickCount64()) + L"_" +
                 std::to_wstring(sequence.fetch_add(1u)));
        std::filesystem::create_directories(path_);
    }

    ~ScopedTestDirectory() {
        static const std::wstring prefix = L"rl78_session_logger_";
        if (!path_.is_absolute() || path_ == path_.root_path() ||
            path_.filename().wstring().rfind(prefix, 0u) != 0u) {
            return;
        }
        std::error_code temp_error;
        const auto temp_path = std::filesystem::temp_directory_path(temp_error);
        if (temp_error) {
            return;
        }
        std::error_code canonical_temp_error;
        const auto canonical_temp =
            std::filesystem::weakly_canonical(temp_path, canonical_temp_error);
        std::error_code canonical_path_error;
        const auto canonical_path =
            std::filesystem::weakly_canonical(path_, canonical_path_error);
        if (canonical_temp_error || canonical_path_error ||
            canonical_path == canonical_path.root_path() ||
            canonical_path.parent_path() != canonical_temp ||
            canonical_path.filename().wstring().rfind(prefix, 0u) != 0u) {
            return;
        }
        std::error_code error;
        std::filesystem::remove_all(canonical_path, error);
    }

    ScopedTestDirectory(const ScopedTestDirectory&) = delete;
    ScopedTestDirectory& operator=(const ScopedTestDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& Path() const {
        return path_;
    }

private:
    std::filesystem::path path_;
};

std::vector<std::uint8_t> ReadBinaryFile(const std::filesystem::path& path) {
    const auto length = std::filesystem::file_size(path);
    if (length > static_cast<std::uintmax_t>((std::numeric_limits<std::size_t>::max)())) {
        throw std::runtime_error("test file is too large");
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open test file");
    }
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    if (!input) {
        throw std::runtime_error("cannot read test file");
    }
    return bytes;
}

void WriteBinaryFile(const std::filesystem::path& path,
                     const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot create test file");
    }
    if (!bytes.empty()) {
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
    if (!output) {
        throw std::runtime_error("cannot write test file");
    }
}

void TestSessionFileLogger() {
    using rl78_programmer::app::SessionFileLogger;

    ScopedTestDirectory temporary;
    const auto root = temporary.Path();
    const auto unused_directory = root / L"default_closed";
    {
        SessionFileLogger logger;
        std::wstring error;
        Check(!logger.IsOpen() && logger.Path().empty() &&
                  !logger.AppendLine(L"must not be written", &error) &&
                  !error.empty() && !std::filesystem::exists(unused_directory),
              "Session logger is closed by default and creates no file");
    }

    {
        SessionFileLogger logger;
        std::wstring error;
        const auto directory = root / L"unicode";
        Check(logger.Open(directory, L"session", &error) &&
                  logger.IsOpen() && error.empty(),
              "Session logger creates a new writable log");
        const auto path = logger.Path();
        Check(logger.AppendLine(L"Unicode \u4E2D\u6587 \U0001F600", &error),
              "Session logger appends Unicode text");

        const std::vector<std::uint8_t> expected_first{
            0xEFu, 0xBBu, 0xBFu,
            0x55u, 0x6Eu, 0x69u, 0x63u, 0x6Fu, 0x64u, 0x65u, 0x20u,
            0xE4u, 0xB8u, 0xADu, 0xE6u, 0x96u, 0x87u, 0x20u,
            0xF0u, 0x9Fu, 0x98u, 0x80u, 0x0Du, 0x0Au
        };
        Check(ReadBinaryFile(path) == expected_first,
              "Session logger flushes UTF-8 BOM and CRLF while open");

        Check(logger.AppendLine(L"second", &error),
              "Session logger appends a second line");
        auto expected_all = expected_first;
        const std::vector<std::uint8_t> second{
            0x73u, 0x65u, 0x63u, 0x6Fu, 0x6Eu, 0x64u, 0x0Du, 0x0Au
        };
        expected_all.insert(expected_all.end(), second.begin(), second.end());
        Check(ReadBinaryFile(path) == expected_all,
              "Session logger writes the UTF-8 BOM exactly once");
        logger.Close();
    }

    {
        const auto directory = root / L"collision";
        std::filesystem::create_directories(directory);
        const std::vector<std::uint8_t> original{0x6Fu, 0x6Cu, 0x64u};
        const std::vector<std::uint8_t> original_suffix{0x6Fu, 0x6Cu, 0x64u, 0x31u};
        WriteBinaryFile(directory / L"session.log", original);
        WriteBinaryFile(directory / L"session_001.log", original_suffix);

        SessionFileLogger logger;
        std::wstring error;
        Check(logger.Open(directory, L"session", &error) &&
                  logger.Path().filename() == L"session_002.log",
              "Session logger resolves filename collisions atomically");
        logger.Close();
        Check(ReadBinaryFile(directory / L"session.log") == original &&
                  ReadBinaryFile(directory / L"session_001.log") == original_suffix,
              "Session logger never overwrites colliding files");
    }

    {
        const auto blocker = root / L"not_a_directory";
        WriteBinaryFile(blocker, std::vector<std::uint8_t>{0x58u});
        const auto invalid_directory = blocker / L"logs";
        SessionFileLogger logger;
        std::wstring error;
        Check(!logger.Open(invalid_directory, L"session", &error) &&
                  !logger.IsOpen() &&
                  error.find(invalid_directory.wstring()) != std::wstring::npos,
              "Session logger reports the unwritable path without fallback");
    }

    {
        SessionFileLogger logger;
        std::wstring error;
        const auto directory = root / L"close";
        Check(logger.Open(directory, L"session", &error) &&
                  logger.AppendLine(L"before close", &error),
              "Session logger writes before close");
        const auto path = logger.Path();
        logger.Close();
        logger.Close();
        const auto closed_bytes = ReadBinaryFile(path);
        Check(!logger.IsOpen() && logger.Path() == path &&
                  !logger.AppendLine(L"after close", &error) &&
                  !error.empty() && ReadBinaryFile(path) == closed_bytes,
              "Session logger close is idempotent and rejects later writes");
    }

    {
        constexpr unsigned kThreadCount = 4u;
        constexpr unsigned kLinesPerThread = 20u;
        SessionFileLogger logger;
        std::wstring error;
        const auto directory = root / L"concurrent";
        Check(logger.Open(directory, L"session", &error),
              "Session logger opens for concurrent writers");
        const auto path = logger.Path();
        std::atomic<bool> writes_succeeded{true};
        std::vector<std::thread> threads;
        for (unsigned thread_index = 0u;
             thread_index < kThreadCount;
             ++thread_index) {
            threads.emplace_back([&logger, &writes_succeeded, thread_index]() {
                for (unsigned line_index = 0u;
                     line_index < kLinesPerThread;
                     ++line_index) {
                    const std::wstring line = L"T" + std::to_wstring(thread_index) +
                                              L":" + std::to_wstring(line_index);
                    std::wstring append_error;
                    if (!logger.AppendLine(line, &append_error)) {
                        writes_succeeded.store(false);
                    }
                }
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }
        logger.Close();

        const auto bytes = ReadBinaryFile(path);
        std::set<std::string> actual_lines;
        std::size_t actual_record_count = 0u;
        bool valid_records = bytes.size() >= 3u &&
                             bytes[0] == 0xEFu &&
                             bytes[1] == 0xBBu &&
                             bytes[2] == 0xBFu;
        if (valid_records) {
            const std::string text(bytes.begin() + 3, bytes.end());
            std::size_t offset = 0u;
            while (offset < text.size()) {
                const auto end = text.find("\r\n", offset);
                if (end == std::string::npos) {
                    valid_records = false;
                    break;
                }
                actual_lines.insert(text.substr(offset, end - offset));
                ++actual_record_count;
                offset = end + 2u;
            }
        }
        std::set<std::string> expected_lines;
        for (unsigned thread_index = 0u;
             thread_index < kThreadCount;
             ++thread_index) {
            for (unsigned line_index = 0u;
                 line_index < kLinesPerThread;
                 ++line_index) {
                expected_lines.insert("T" + std::to_string(thread_index) +
                                      ":" + std::to_string(line_index));
            }
        }
        Check(writes_succeeded.load() && valid_records &&
                  actual_record_count == kThreadCount * kLinesPerThread &&
                  actual_lines == expected_lines,
              "Session logger serializes concurrent complete records");
    }
}

void TestValidatedImageSource() {
    rl78_programmer::app::ValidatedImageSource source;
    Check(!source.Matches(L"fixtures\\P20.mot"),
          "unbound image source rejects programming path");
    source.Bind(L"fixtures\\P20.mot");
    Check(source.Matches(L"fixtures\\P20.mot"),
          "validated image source accepts exact bound path");
    Check(!source.Matches(L"fixtures\\P21.mot"),
          "changed image path invalidates stale package use");
    source.Clear();
    Check(!source.Matches(L"fixtures\\P20.mot"),
          "cleared image source rejects former path");
}

std::string Hex(const std::uint8_t* data, std::size_t size) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < size; ++i) {
        out << std::setw(2) << static_cast<unsigned>(data[i]);
    }
    return out.str();
}

std::string MakeS1(std::uint16_t address, const std::vector<std::uint8_t>& data) {
    const std::uint8_t count = static_cast<std::uint8_t>(data.size() + 3u);
    std::uint32_t sum = count + static_cast<std::uint8_t>(address >> 8) +
                        static_cast<std::uint8_t>(address);
    std::ostringstream out;
    out << 'S' << '1' << std::uppercase << std::hex << std::setfill('0')
        << std::setw(2) << static_cast<unsigned>(count)
        << std::setw(4) << static_cast<unsigned>(address);
    for (const auto value : data) {
        sum += value;
        out << std::setw(2) << static_cast<unsigned>(value);
    }
    const auto checksum = static_cast<std::uint8_t>(~sum);
    out << std::setw(2) << static_cast<unsigned>(checksum);
    return out.str();
}

std::string MakeS9(std::uint16_t address) {
    const std::uint8_t count = 3;
    const std::uint32_t sum = count + static_cast<std::uint8_t>(address >> 8) +
                              static_cast<std::uint8_t>(address);
    std::ostringstream out;
    out << "S9" << std::uppercase << std::hex << std::setfill('0')
        << std::setw(2) << static_cast<unsigned>(count)
        << std::setw(4) << static_cast<unsigned>(address)
        << std::setw(2) << static_cast<unsigned>(static_cast<std::uint8_t>(~sum));
    return out.str();
}

std::string MakeIntelHex(std::uint16_t address,
                         std::uint8_t type,
                         const std::vector<std::uint8_t>& data) {
    if (data.size() > 255u) {
        throw std::runtime_error("Intel HEX test record is too large");
    }
    const auto count = static_cast<std::uint8_t>(data.size());
    std::uint32_t sum = count + static_cast<std::uint8_t>(address >> 8u) +
                        static_cast<std::uint8_t>(address) + type;
    std::ostringstream out;
    out << ':' << std::uppercase << std::hex << std::setfill('0')
        << std::setw(2) << static_cast<unsigned>(count)
        << std::setw(4) << static_cast<unsigned>(address)
        << std::setw(2) << static_cast<unsigned>(type);
    for (const auto value : data) {
        sum += value;
        out << std::setw(2) << static_cast<unsigned>(value);
    }
    const auto checksum = static_cast<std::uint8_t>(0u - sum);
    out << std::setw(2) << static_cast<unsigned>(checksum);
    return out.str();
}

bool IntelHexRejected(const std::string& source) {
    try {
        static_cast<void>(rl78_programmer::ParseIntelHex(source));
    } catch (const rl78_programmer::IntelHexParseError&) {
        return true;
    }
    return false;
}

void TestIntegrity() {
    const std::string check = "123456789";
    Check(Rpp_Crc16CcittFalse(reinterpret_cast<const std::uint8_t*>(check.data()), check.size()) == 0x29B1u,
          "CRC16 known answer");
    Check(Rpp_Crc32IsoHdlc(reinterpret_cast<const std::uint8_t*>(check.data()), check.size()) == 0xCBF43926u,
          "CRC32 known answer");

    const std::string abc = "abc";
    std::array<std::uint8_t, 32> digest{};
    Rpp_Sha256(reinterpret_cast<const std::uint8_t*>(abc.data()), abc.size(), digest.data());
    Check(Hex(digest.data(), digest.size()) ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "SHA-256 known answer");
}

void WriteLe32(std::vector<std::uint8_t>* bytes,
               std::size_t offset,
               std::uint32_t value) {
    (*bytes)[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    (*bytes)[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
    (*bytes)[offset + 2u] = static_cast<std::uint8_t>((value >> 16u) & 0xFFu);
    (*bytes)[offset + 3u] = static_cast<std::uint8_t>((value >> 24u) & 0xFFu);
}

std::vector<std::uint8_t> MakeBridgeApplication(
    const rl78_programmer::sdk::BridgeFirmwareProfile& profile) {
    std::vector<std::uint8_t> bytes(profile.application_size, 0xFFu);
    for (std::size_t index = 0; index < profile.crc32_offset; ++index) {
        bytes[index] = static_cast<std::uint8_t>((index * 17u + 3u) & 0xFFu);
    }
    const std::uint32_t crc =
        Rpp_Crc32IsoHdlc(bytes.data(), profile.crc32_offset);
    WriteLe32(&bytes, profile.crc32_offset, crc);
    return bytes;
}

void TestBridgeFirmwareUpdateContracts() {
    using rl78_programmer::sdk::BridgeFirmwareTarget;
    using rl78_programmer::sdk::BridgeFirmwareUpdater;
    namespace nuvo = rl78_programmer::sdk::detail::nuvo_isp;

    const auto m032 = BridgeFirmwareUpdater::Profile(
        BridgeFirmwareTarget::M032SE3AE);
    const auto m484 = BridgeFirmwareUpdater::Profile(
        BridgeFirmwareTarget::M484SIDAE);
    const auto m032_test = BridgeFirmwareUpdater::Profile(
        BridgeFirmwareTarget::M032TestBoard);
    Check(m032.application_start == 0x4000u &&
              m032.application_size == 0xBC00u &&
              m032.crc32_offset == 0xBBFCu,
          "M032 bridge firmware profile uses the exact application partition");
    Check(m484.application_start == 0x4000u &&
              m484.application_size == 0x7C000u &&
              m484.crc32_offset == 0x7BFFCu,
          "M484 bridge firmware profile uses the exact application partition");
    Check(m032_test.application_start == 0x4000u &&
              m032_test.application_size == 0xBC00u &&
              m032_test.crc32_offset == 0xBBFCu &&
              m032_test.expected_board_id == 0x3142544Du,
          "M032 TEST BOARD firmware profile requires its UART boot identity");

    rl78_programmer::sdk::BridgeApplicationGeometry m032_geometry;
    m032_geometry.application_start = 0x4000u;
    m032_geometry.application_size = 0xBC00u;
    m032_geometry.application_end = 0xFC00u;
    m032_geometry.crc32_address = 0xFBFCu;
    bool matching_geometry_accepted = true;
    try {
        BridgeFirmwareUpdater::ValidateCompatibility(m032, m032_geometry);
    } catch (const rl78_programmer::sdk::ProgrammerException&) {
        matching_geometry_accepted = false;
    }
    Check(matching_geometry_accepted,
          "Bridge updater accepts exact bootloader application geometry");

    auto m032_test_geometry = m032_geometry;
    m032_test_geometry.board_id = 0x3142544Du;
    bool m032_test_geometry_accepted = true;
    try {
        BridgeFirmwareUpdater::ValidateCompatibility(
            m032_test, m032_test_geometry);
    } catch (const rl78_programmer::sdk::ProgrammerException&) {
        m032_test_geometry_accepted = false;
    }
    Check(m032_test_geometry_accepted,
          "M032 TEST BOARD updater accepts matching UART boot identity");

    auto wrong_board_id = m032_test_geometry;
    wrong_board_id.board_id = 0u;
    bool wrong_board_id_rejected = false;
    try {
        BridgeFirmwareUpdater::ValidateCompatibility(
            m032_test, wrong_board_id);
    } catch (const rl78_programmer::sdk::ProgrammerException& error) {
        wrong_board_id_rejected =
            error.status() == PLP_ERR_UPDATE_INCOMPATIBLE;
    }
    Check(wrong_board_id_rejected,
          "M032 TEST BOARD updater rejects a foreign UART bootloader");

    const auto geometry_rejected = [&m032](
        const rl78_programmer::sdk::BridgeApplicationGeometry& geometry) {
        try {
            BridgeFirmwareUpdater::ValidateCompatibility(m032, geometry);
        } catch (const rl78_programmer::sdk::ProgrammerException& error) {
            return error.status() == PLP_ERR_UPDATE_INCOMPATIBLE;
        }
        return false;
    };
    auto wrong_start = m032_geometry;
    auto wrong_size = m032_geometry;
    auto wrong_end = m032_geometry;
    auto wrong_crc = m032_geometry;
    ++wrong_start.application_start;
    ++wrong_size.application_size;
    ++wrong_end.application_end;
    ++wrong_crc.crc32_address;
    Check(geometry_rejected(wrong_start) && geometry_rejected(wrong_size) &&
              geometry_rejected(wrong_end) && geometry_rejected(wrong_crc),
          "Bridge updater rejects any profile geometry mismatch before programming");

    const auto iap_config_rejected = [](BridgeFirmwareTarget target,
                                        std::uint32_t config0) {
        try {
            BridgeFirmwareUpdater::ValidateIapConfiguration(target, config0);
        } catch (const rl78_programmer::sdk::ProgrammerException& error) {
            return error.status() == PLP_ERR_UPDATE_INCOMPATIBLE;
        }
        return false;
    };
    bool m032_iap_config_accepted = true;
    bool m484_iap_config_accepted = true;
    bool m032_mbs_clear_accepted = true;
    try {
        BridgeFirmwareUpdater::ValidateIapConfiguration(
            BridgeFirmwareTarget::M032SE3AE, 0xFFFFFFBFu);
    } catch (const rl78_programmer::sdk::ProgrammerException&) {
        m032_iap_config_accepted = false;
    }
    try {
        BridgeFirmwareUpdater::ValidateIapConfiguration(
            BridgeFirmwareTarget::M484SIDAE, 0xFFFFFFBFu);
    } catch (const rl78_programmer::sdk::ProgrammerException&) {
        m484_iap_config_accepted = false;
    }
    try {
        BridgeFirmwareUpdater::ValidateIapConfiguration(
            BridgeFirmwareTarget::M032SE3AE, 0xFFFFFF9Fu);
    } catch (const rl78_programmer::sdk::ProgrammerException&) {
        m032_mbs_clear_accepted = false;
    }
    Check(m032_iap_config_accepted && m484_iap_config_accepted,
          "Bridge updater accepts CONFIG0 CBS=10 and M484 MBS=1");
    Check(iap_config_rejected(BridgeFirmwareTarget::M032SE3AE, 0xFFFFFFFFu) &&
              iap_config_rejected(BridgeFirmwareTarget::M484SIDAE, 0xFFFFFFFFu),
          "Bridge updater rejects CONFIG0 without APROM IAP");
    Check(iap_config_rejected(BridgeFirmwareTarget::M484SIDAE, 0xFFFFFF9Fu),
          "Bridge updater rejects M484 CONFIG0 with MBS cleared");
    Check(m032_mbs_clear_accepted,
          "Bridge updater does not apply the M484 MBS rule to M032");

    const auto m032_bytes = MakeBridgeApplication(m032);
    const auto m032_image = BridgeFirmwareUpdater::ValidateImage(
        BridgeFirmwareTarget::M032SE3AE, m032_bytes);
    Check(m032_image.stored_crc32 == m032_image.calculated_crc32 &&
              m032_image.bytes.size() == 0xBC00u &&
              m032_image.additive_checksum ==
                  nuvo::AdditiveChecksum(m032_bytes.data(), m032_bytes.size()),
          "M032 bridge application validates CRC32 and additive checksum");

    const auto m484_bytes = MakeBridgeApplication(m484);
    const auto m484_image = BridgeFirmwareUpdater::ValidateImage(
        BridgeFirmwareTarget::M484SIDAE, m484_bytes);
    Check(m484_image.stored_crc32 == m484_image.calculated_crc32 &&
              m484_image.bytes.size() == 0x7C000u,
          "M484 bridge application validates the fixed-size CRC32 image");

    bool bad_size_rejected = false;
    try {
        std::vector<std::uint8_t> bad_size(16u, 0u);
        static_cast<void>(BridgeFirmwareUpdater::ValidateImage(
            BridgeFirmwareTarget::M032SE3AE, bad_size));
    } catch (const rl78_programmer::sdk::ProgrammerException& error) {
        bad_size_rejected = error.status() == PLP_ERR_UPDATE_INCOMPATIBLE;
    }
    Check(bad_size_rejected,
          "Bridge firmware validation rejects a profile-size mismatch");

    bool bad_crc_rejected = false;
    try {
        auto bad_crc = m032_bytes;
        bad_crc[123u] ^= 0x01u;
        static_cast<void>(BridgeFirmwareUpdater::ValidateImage(
            BridgeFirmwareTarget::M032SE3AE, bad_crc));
    } catch (const rl78_programmer::sdk::ProgrammerException& error) {
        bad_crc_rejected = error.status() == PLP_ERR_IMAGE_CHECKSUM;
    }
    Check(bad_crc_rejected,
          "Bridge firmware validation rejects a CRC32 mismatch");

    std::array<std::uint8_t, 13> payload{};
    nuvo::WriteLe32(payload.data(), m032.application_start);
    nuvo::WriteLe32(payload.data() + 4u, m032.application_size);
    payload[8] = 0x10u;
    payload[9] = 0x20u;
    payload[10] = 0x30u;
    payload[11] = 0x40u;
    payload[12] = 0x50u;
    const auto request = nuvo::BuildPacket(nuvo::kCommandUpdateAprom, 3u,
                                           payload.data(), payload.size());
    nuvo::Packet response{};
    const std::uint16_t request_checksum =
        nuvo::AdditiveChecksum(request.data(), request.size());
    response[0] = static_cast<std::uint8_t>(request_checksum & 0xFFu);
    response[1] = static_cast<std::uint8_t>(request_checksum >> 8u);
    nuvo::WriteLe32(response.data() + 4u, 4u);
    const auto valid_response = nuvo::ValidateResponse(request, response, 4u, true);
    Check(nuvo::ReadLe32(request.data()) == nuvo::kCommandUpdateAprom &&
              nuvo::ReadLe32(request.data() + 4u) == 3u &&
              nuvo::ReadLe32(request.data() + 8u) == 0x4000u &&
              nuvo::ReadLe32(request.data() + 12u) == 0xBC00u &&
              valid_response.checksum_ok && valid_response.index_ok,
          "NuvoISP application-range packet and response validation golden vector");

    response[4] = 5u;
    const auto wrong_index = nuvo::ValidateResponse(request, response, 4u, true);
    Check(wrong_index.checksum_ok && !wrong_index.index_ok,
          "NuvoISP response validation rejects the wrong packet index");
}

void TestPlp() {
    std::array<std::uint8_t, PLP_REPORT_SIZE> report{};
    Check(Rpp_PlpEncode(1, 0, 0, PLP_CMD_PING, 1, nullptr, 0, report.data()) == PLP_OK,
          "PLP PING encode");
    Check(report[62] == 0x3Du && report[63] == 0xE8u, "PLP PING golden CRC");

    RppPlpFrame decoded{};
    Check(Rpp_PlpDecode(report.data(), &decoded) == PLP_OK &&
              decoded.command == PLP_CMD_PING && decoded.sequence == 1,
          "PLP PING decode");

    report[20] = 1;
    Rpp_WriteLe16(report.data() + PLP_CRC_OFFSET,
                  Rpp_Crc16CcittFalse(report.data(), PLP_CRC_OFFSET));
    Check(Rpp_PlpDecode(report.data(), &decoded) == PLP_ERR_BAD_LENGTH,
          "PLP rejects nonzero padding");

    std::array<std::uint8_t, PLP_REPORT_SIZE> response{};
    Check(Rpp_PlpBuildResponse(PLP_CMD_PING, 1, PLP_STATE_READY, PLP_OK,
                               0, 0, nullptr, 0, response.data()) == PLP_OK,
          "PLP response encode");
    Check(response[62] == 0xA1u && response[63] == 0x71u,
          "PLP response golden CRC");
}

void TestSerialPlpFramer() {
    using rl78_programmer::sdk::detail::SerialPlpFramer;

    std::array<std::uint8_t, PLP_REPORT_SIZE> first{};
    std::array<std::uint8_t, PLP_REPORT_SIZE> second{};
    std::array<std::uint8_t, PLP_REPORT_SIZE> decoded{};
    const std::array<std::uint8_t, 4> payload{0x12u, 0x34u, 0x56u, 0x78u};
    Check(Rpp_PlpEncode(PLP_VERSION_MAJOR, PLP_VERSION_MINOR, 0u,
                        PLP_CMD_PING, 7u, payload.data(),
                        static_cast<std::uint16_t>(payload.size()),
                        first.data()) == PLP_OK &&
              Rpp_PlpEncode(PLP_VERSION_MAJOR, PLP_VERSION_MINOR, 0u,
                            PLP_CMD_GET_STATUS, 8u, nullptr, 0u,
                            second.data()) == PLP_OK,
          "UART PLP framer fixtures encode");

    SerialPlpFramer framer;
    const std::array<std::uint8_t, 5> noise{
        0x00u, 0xFFu, 0xA4u, 0x5Au, 0x10u};
    framer.Push(noise.data(), noise.size());
    framer.Push(first.data(), 17u);
    Check(!framer.TryTake(&decoded),
          "UART PLP framer waits for a fragmented report");
    framer.Push(first.data() + 17u, first.size() - 17u);
    Check(framer.TryTake(&decoded) && decoded == first,
          "UART PLP framer removes leading noise and joins fragments");

    auto corrupt = first;
    corrupt[PLP_REPORT_SIZE - 1u] ^= 0x80u;
    std::vector<std::uint8_t> recovery;
    recovery.insert(recovery.end(), corrupt.begin(), corrupt.end());
    recovery.insert(recovery.end(), second.begin(), second.end());
    framer.Push(recovery.data(), recovery.size());
    Check(framer.TryTake(&decoded) && decoded == second &&
              framer.BufferedByteCount() == 0u,
          "UART PLP framer resynchronizes after a CRC-damaged report");
}

rl78_programmer::SRecordImage BuildTestImage() {
    rl78_programmer::SRecordImage image;
    image.segments.push_back({0x0000u, {0x34, 0x12, 0x00, 0x01, 0x02, 0x03}});
    image.segments.push_back({0x00C0u, {0xF7, 0xE7, 0xF9, 0x85}});
    image.segments.push_back({0x09000u, {0x10, 0x20, 0x30, 0x40}});
    image.source_record_count = 3;
    return image;
}

void TestSRecord() {
    const std::string source = MakeS1(0x0000u, {0x10, 0x20, 0x30, 0x40}) + "\r\n" +
                               MakeS1(0x0004u, {0x50, 0x60}) + "\n" +
                               MakeS9(0x0000u) + "\n";
    const auto image = rl78_programmer::ParseSRecord(source);
    Check(image.segments.size() == 1 && image.DataSize() == 6 &&
              image.HighestAddressExclusive() == 6,
          "S-record contiguous segments");
    Check(image.has_entry_address && image.entry_address == 0,
          "S-record termination address");

    bool duplicate_rejected = false;
    try {
        const std::string duplicate = MakeS1(0x0000u, {0x10}) + "\n" +
                                      MakeS1(0x0000u, {0x10}) + "\n" + MakeS9(0) + "\n";
        static_cast<void>(rl78_programmer::ParseSRecord(duplicate));
    } catch (const rl78_programmer::ImageParseError&) {
        duplicate_rejected = true;
    }
    Check(duplicate_rejected, "S-record duplicate rejection");
}

void TestIntelHex() {
    const std::string actual_record =
        ":0400C000EFF7F985D8\r\n:00000001FF\r\n";
    const auto actual_image = rl78_programmer::ParseAddressedTextImage(
        std::string("\xEF\xBB\xBF  \r\n") + actual_record);
    Check(actual_image.segments.size() == 1u &&
              actual_image.segments[0].address == 0x00C0u &&
              actual_image.segments[0].data ==
                  std::vector<std::uint8_t>({0xEFu, 0xF7u, 0xF9u, 0x85u}),
          "Intel HEX detects colon-formatted MOT content");

    std::vector<std::uint8_t> long_record_data(255u);
    for (std::size_t index = 0u; index < long_record_data.size(); ++index) {
        long_record_data[index] = static_cast<std::uint8_t>(index);
    }
    const std::string long_record = MakeIntelHex(0x0100u, 0x00u, long_record_data) +
                                    "\n" + MakeIntelHex(0u, 0x01u, {}) + "\n";
    const auto long_image = rl78_programmer::ParseAddressedTextImage(long_record);
    Check(long_image.DataSize() == 255u && long_image.LowestAddress() == 0x0100u &&
              long_image.HighestAddressExclusive() == 0x01FFu,
          "Intel HEX accepts 255-byte data records used by RL78 MOT output");

    const std::string addressed =
        MakeIntelHex(0u, 0x02u, {0x10u, 0x00u}) + "\n" +
        MakeIntelHex(0x20u, 0x00u, {0xAAu}) + "\n" +
        MakeIntelHex(0u, 0x04u, {0x00u, 0x0Fu}) + "\n" +
        MakeIntelHex(0xFFF0u, 0x00u,
                     {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u,
                      8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u}) + "\n" +
        MakeIntelHex(0u, 0x05u, {0x00u, 0x01u, 0x00u, 0x20u}) + "\n" +
        MakeIntelHex(0u, 0x01u, {}) + "\n";
    const auto addressed_image = rl78_programmer::ParseIntelHex(addressed);
    Check(addressed_image.DataSize() == 17u &&
              addressed_image.LowestAddress() == 0x10020u &&
              addressed_image.HighestAddressExclusive() == 0x100000u,
          "Intel HEX extended segment and linear address mapping");
    Check(addressed_image.has_entry_address &&
              addressed_image.entry_address == 0x10020u,
          "Intel HEX start linear address");

    std::string bad_checksum = actual_record;
    bad_checksum[17] = bad_checksum[17] == '0' ? '1' : '0';
    Check(IntelHexRejected(bad_checksum), "Intel HEX checksum rejection");
    std::string bad_count = MakeIntelHex(0u, 0x00u, {0x11u});
    bad_count.replace(1u, 2u, "02");
    Check(IntelHexRejected(bad_count), "Intel HEX byte-count rejection");
    Check(IntelHexRejected(MakeIntelHex(0u, 0x00u, {0x11u})),
          "Intel HEX requires EOF");
    Check(IntelHexRejected(MakeIntelHex(0u, 0x00u, {0x11u}) + "\n" +
                               MakeIntelHex(0u, 0x01u, {}) + "\n" +
                               MakeIntelHex(1u, 0x00u, {0x22u})),
          "Intel HEX rejects content after EOF");
    Check(IntelHexRejected(MakeIntelHex(0u, 0x00u, {0x11u}) + "\n" +
                               MakeIntelHex(0u, 0x00u, {0x11u}) + "\n" +
                               MakeIntelHex(0u, 0x01u, {})),
          "Intel HEX duplicate rejection");
    Check(IntelHexRejected(MakeIntelHex(0u, 0x04u, {0x00u, 0x0Fu}) + "\n" +
                               MakeIntelHex(0xFFFFu, 0x00u, {0x11u, 0x22u}) + "\n" +
                               MakeIntelHex(0u, 0x01u, {})),
          "Intel HEX rejects RL78 address overflow");
    Check(IntelHexRejected(MakeIntelHex(0u, 0x06u, {}) + "\n" +
                               MakeIntelHex(0u, 0x01u, {})),
          "Intel HEX unsupported type rejection");
    Check(IntelHexRejected(MakeIntelHex(1u, 0x04u, {0x00u, 0x01u}) + "\n" +
                               MakeIntelHex(0u, 0x01u, {})),
          "Intel HEX control-record validation");

    const std::string srecord = MakeS1(0u, {0x5Au}) + "\n" + MakeS9(0u) + "\n";
    const auto detected_srecord = rl78_programmer::ParseAddressedTextImage(srecord);
    Check(detected_srecord.DataSize() == 1u && detected_srecord.segments[0].data[0] == 0x5Au,
          "Text image detection preserves S-record parsing");

    bool unsupported_rejected = false;
    try {
        static_cast<void>(rl78_programmer::ParseAddressedTextImage("not an addressed image"));
    } catch (const std::runtime_error&) {
        unsupported_rejected = true;
    }
    Check(unsupported_rejected, "Text image detection never falls back to raw binary");

    const std::string sparse_g15 =
        MakeIntelHex(0u, 0x00u, {0x5Au}) + "\n" +
        MakeIntelHex(0x1FFFu, 0x00u, {0xA5u}) + "\n" +
        MakeIntelHex(0u, 0x01u, {}) + "\n";
    const auto sparse_image = rl78_programmer::ParseIntelHex(sparse_g15);
    rl78_programmer::OipBuildOptions options;
    options.target_profile = rl78_programmer::Rl78G15_8KTargetProfile();
    const auto package = rl78_programmer::BuildOfflineImagePackage(sparse_image, options);
    Check(!package.bytes.empty(), "Intel HEX sparse 8 KB G15 image builds OIP");
    bool four_k_rejected = false;
    try {
        options.target_profile = rl78_programmer::Rl78G15_4KTargetProfile();
        static_cast<void>(rl78_programmer::BuildOfflineImagePackage(sparse_image, options));
    } catch (const std::runtime_error&) {
        four_k_rejected = true;
    }
    Check(four_k_rejected, "Intel HEX sparse 8 KB image is rejected by 4 KB profile");
}

void TestOip() {
    rl78_programmer::OipBuildOptions options;
    options.project_name = "Conformance";
    options.firmware_version = "1.2.3";
    options.build_time_ms = 123456789u;
    const auto package = rl78_programmer::BuildOfflineImagePackage(BuildTestImage(), options);

    RppOipInfo info{};
    Check(Rpp_OipValidateBasic(package.bytes.data(), package.bytes.size(), &info) == PLP_OK,
          "OIP validates");
    Check(info.section_count == 3 && info.package_size == package.bytes.size(),
          "OIP section classification");

    auto corrupted = package.bytes;
    corrupted.back() ^= 0x01u;
    Check(Rpp_OipValidateBasic(corrupted.data(), corrupted.size(), &info) == PLP_ERR_INTEGRITY_CRC,
          "OIP payload corruption rejection");

    auto extended_profile = rl78_programmer::Rl78G15_8KTargetProfile();
    extended_profile.profile_id = "native-extended-profile";
    extended_profile.display_name = "Native extended target";
    extended_profile.driver_requirement = "native-driver/1.0.0";
    extended_profile.uuid[15] = 0x7E;
    extended_profile.regions[2].end_address = 0x03FFFu;
    rl78_programmer::SRecordImage extended_image;
    extended_image.segments.push_back({0x02000u, {0xAA, 0x55}});
    options.target_profile = extended_profile;
    const auto extended_package = rl78_programmer::BuildOfflineImagePackage(extended_image, options);
    Check(Rpp_OipValidateBasic(extended_package.bytes.data(), extended_package.bytes.size(),
                               &info) == PLP_OK &&
              std::equal(extended_profile.uuid.begin(), extended_profile.uuid.end(),
                         info.target_profile_uuid),
          "OIP builder uses supplied target profile instead of fixed 8 KB map");
}

struct FakeTarget {
    int polls = 0;
    bool cancel = false;
    bool fail = false;
    std::uint8_t source_kind = 0xFF;
    int start_calls = 0;
    int retry_connect_calls = 0;
    ProgrammerTargetFailureSnapshot failure_snapshot{};
};

struct FakeModeStore {
    std::uint8_t mode = PLP_OPERATING_MODE_ONLINE_ONLY;
    std::uint8_t autorun = 0;
    std::uint32_t generation = 0;
};

struct FakeBoot {
    std::uint32_t result = PLP_OK;
    unsigned prepare_calls = 0;
    std::uint32_t reset_result = PLP_OK;
    unsigned reset_calls = 0;
};

std::uint32_t FakePrepareBoot(void* context) {
    auto* boot = static_cast<FakeBoot*>(context);
    ++boot->prepare_calls;
    return boot->result;
}

std::uint32_t FakePrepareReset(void* context) {
    auto* boot = static_cast<FakeBoot*>(context);
    ++boot->reset_calls;
    return boot->reset_result;
}

std::uint32_t FakeModeLoad(void* context, std::uint8_t* mode,
                           std::uint8_t* autorun, std::uint32_t* generation) {
    auto* store = static_cast<FakeModeStore*>(context);
    *mode = store->mode;
    *autorun = store->autorun;
    *generation = store->generation;
    return PLP_OK;
}

std::uint32_t FakeModeWrite(void* context, std::uint8_t mode,
                            std::uint8_t autorun, std::uint32_t expected_generation,
                            std::uint32_t* new_generation) {
    auto* store = static_cast<FakeModeStore*>(context);
    if (expected_generation != store->generation) return PLP_ERR_CONFIG_PERSIST;
    store->mode = mode;
    store->autorun = autorun;
    ++store->generation;
    *new_generation = store->generation;
    return PLP_OK;
}

std::uint32_t FakeReadSignature(void*, const std::uint8_t requested[16],
                                std::uint8_t matched[16], std::uint8_t digest[16],
                                std::uint16_t* raw_length, std::uint16_t* flags) {
    std::copy_n(requested, 16, matched);
    std::fill_n(digest, 16, std::uint8_t{0x5A});
    *raw_length = 16;
    *flags = 1;
    return PLP_OK;
}

std::uint32_t FakeImageRead(void*, std::uint32_t, std::uint8_t* data,
                            std::uint32_t length) {
    std::fill_n(data, length, std::uint8_t{0xFF});
    return PLP_OK;
}

std::uint8_t FakeImageHasRegion(void*, std::uint16_t) {
    return 0;
}

std::uint32_t FakeOpenImage(void*, std::uint8_t, const std::uint8_t[16],
                            ProgrammerImageOps* image) {
    const auto& profile = rl78_programmer::Rl78G15_8KTargetProfile();
    std::fill_n(reinterpret_cast<std::uint8_t*>(image), sizeof(*image),
                std::uint8_t{0});
    std::copy(profile.uuid.begin(), profile.uuid.end(), image->target_profile_uuid);
    image->read_target = FakeImageRead;
    image->has_region = FakeImageHasRegion;
    return PLP_OK;
}

std::uint32_t FakeStart(void* context, const ProgrammerImageOps*,
                        std::uint8_t source_kind, const std::uint8_t[16],
                        const std::uint8_t[16], std::uint16_t) {
    auto* target = static_cast<FakeTarget*>(context);
    target->polls = 0;
    target->cancel = false;
    target->source_kind = source_kind;
    ++target->start_calls;
    return PLP_OK;
}

std::uint32_t FakePoll(void* context, ProgrammerTargetStatus* status) {
    auto* target = static_cast<FakeTarget*>(context);
    ++target->polls;
    status->stage = target->fail ? 8 : 4;
    status->progress_basis_points = target->polls >= 2 ? 10000 : 5000;
    status->current = status->progress_basis_points;
    status->total = 10000;
    if (target->cancel) {
        status->terminal = 1;
        status->terminal_result = PLP_TERMINAL_CANCELLED;
    } else if (target->polls >= 2 && target->fail) {
        status->terminal = 1;
        status->terminal_result = PLP_TERMINAL_FAIL;
        status->error = PLP_ERR_TARGET_DIRECTION;
        status->detail = 321;
    } else if (target->polls >= 2) {
        status->terminal = 1;
        status->terminal_result = PLP_TERMINAL_PASS;
    }
    return PLP_OK;
}

std::uint32_t FakeGetFailureSnapshot(
    void* context, ProgrammerTargetFailureSnapshot* snapshot) {
    if (snapshot == nullptr) return PLP_ERR_SDK_ARGUMENT;
    *snapshot = static_cast<FakeTarget*>(context)->failure_snapshot;
    return PLP_OK;
}

std::uint32_t FakeCancel(void* context, std::uint8_t* disposition) {
    static_cast<FakeTarget*>(context)->cancel = true;
    *disposition = 1;
    return PLP_OK;
}

std::uint32_t FakeRetryConnect(void* context) {
    auto* target = static_cast<FakeTarget*>(context);
    ++target->retry_connect_calls;
    target->polls = 0;
    return PLP_OK;
}

std::uint32_t FakeGetActive(void*, std::uint8_t uuid[16], std::uint32_t* generation) {
    std::fill_n(uuid, 16, std::uint8_t{0});
    uuid[0] = 0xA5;
    *generation = 7;
    return PLP_OK;
}

std::uint32_t CoreCommand(ProgrammerCore* core, std::uint16_t command,
                          std::uint16_t sequence, const std::vector<std::uint8_t>& payload,
                          RppPlpFrame* decoded) {
    std::array<std::uint8_t, PLP_REPORT_SIZE> request{};
    std::array<std::uint8_t, PLP_REPORT_SIZE> response{};
    const auto encode = Rpp_PlpEncode(PLP_VERSION_MAJOR, PLP_VERSION_MINOR, 0,
                                      command, sequence,
                                      payload.empty() ? nullptr : payload.data(),
                                      static_cast<std::uint16_t>(payload.size()), request.data());
    if (encode != PLP_OK) return encode;
    const auto handled = ProgrammerCore_HandleReport(core, request.data(), response.data());
    if (handled != PLP_OK) return handled;
    const auto decode = Rpp_PlpDecode(response.data(), decoded);
    if (decode != PLP_OK) return decode;
    return Rpp_ReadLe32(decoded->payload);
}

std::vector<std::uint8_t> ReadDeviceInfo(ProgrammerCore* core) {
    std::vector<std::uint8_t> info;
    std::uint32_t cursor = 0;
    std::uint16_t sequence = 100;
    for (unsigned page_index = 0; page_index < 16; ++page_index) {
        std::vector<std::uint8_t> request(12, std::uint8_t{0});
        RppPlpFrame response{};
        Rpp_WriteLe32(request.data() + 4, cursor);
        request[8] = 35;
        if (CoreCommand(core, PLP_CMD_GET_DEVICE_INFO, sequence++, request,
                        &response) != PLP_OK ||
            response.payload_length < PLP_COMMON_RESPONSE_SIZE + 9u) {
            return {};
        }
        const auto* page = response.payload + PLP_COMMON_RESPONSE_SIZE;
        const auto length = page[8];
        if (response.payload_length < PLP_COMMON_RESPONSE_SIZE + 9u + length) {
            return {};
        }
        info.insert(info.end(), page + 9, page + 9 + length);
        cursor = Rpp_ReadLe32(page + 4);
        if (cursor == 0) {
            return info;
        }
    }
    return {};
}

bool DeviceInfoTlvEquals(const std::vector<std::uint8_t>& info,
                         std::uint16_t expected_type,
                         const std::uint8_t* expected,
                         std::size_t expected_size) {
    std::size_t offset = 0;
    while (offset + 4u <= info.size()) {
        const auto type = Rpp_ReadLe16(info.data() + offset);
        const auto length = info[offset + 3u];
        if (offset + 4u + length > info.size()) return false;
        if (type == expected_type) {
            return length == expected_size &&
                   std::equal(expected, expected + expected_size,
                              info.data() + offset + 4u);
        }
        offset += 4u + length;
    }
    return false;
}

void TestFirmwareCore() {
    ProgrammerCoreConfig config{};
    ProgrammerCore core{};
    FakeTarget target;
    FakeModeStore mode_store;
    config.product_name = "Native M032";
    config.programmer_uuid[0] = 0x32;
    config.capabilities = PLP_CAP_REPLAY_CACHE | PLP_CAP_PAGED_QUERY |
                          PLP_CAP_OFFLINE_PACKAGE | PLP_CAP_OFFLINE_AUTORUN |
                          PLP_CAP_TARGET_PROGRAM;
    config.storage.get_active = FakeGetActive;
    config.storage.open_image = FakeOpenImage;
    config.target.context = &target;
    config.target.read_signature = FakeReadSignature;
    config.target.start = FakeStart;
    config.target.poll = FakePoll;
    config.target.cancel = FakeCancel;
    config.target.get_failure_snapshot = FakeGetFailureSnapshot;
    config.mode.context = &mode_store;
    config.mode.load = FakeModeLoad;
    config.mode.store = FakeModeWrite;
    ProgrammerCore_Init(&core, &config);

    RppPlpFrame response{};
    std::vector<std::uint8_t> negotiate(28, std::uint8_t{0});
    negotiate[0] = 1;
    negotiate[2] = 1;
    Check(CoreCommand(&core, PLP_CMD_NEGOTIATE, 1, negotiate, &response) == PLP_OK &&
              response.payload_length == 40 && core.state == PLP_STATE_READY,
          "Firmware PLP negotiation");

    std::vector<std::uint8_t> session(36, std::uint8_t{0});
    session[0] = 1;
    Check(CoreCommand(&core, PLP_CMD_OPEN_SESSION, 2, session, &response) == PLP_OK &&
              core.state == PLP_STATE_SESSION,
          "Firmware session open");

    std::vector<std::uint8_t> signature(20, std::uint8_t{0});
    Check(CoreCommand(&core, PLP_CMD_TARGET_READ_SIGNATURE, 3, signature, &response) ==
              PLP_ERR_DRIVER_PROFILE,
          "Firmware target fail-closed without profile");

    core.config.target.profile_ready = 1;
    Check(CoreCommand(&core, PLP_CMD_TARGET_READ_SIGNATURE, 4, signature, &response) == PLP_OK &&
              response.payload_length == 44,
          "Firmware target signature callback");

    Check(CoreCommand(&core, PLP_CMD_GET_OPERATING_MODE, 5, {}, &response) == PLP_OK &&
              response.payload[PLP_COMMON_RESPONSE_SIZE] == PLP_OPERATING_MODE_ONLINE_ONLY,
          "Firmware reports Online Only mode over HID");
    std::vector<std::uint8_t> arm_mode(8, std::uint8_t{0});
    arm_mode[0] = PLP_OPERATING_MODE_OFFLINE_ARMED;
    arm_mode[1] = 1;
    Check(CoreCommand(&core, PLP_CMD_SET_OPERATING_MODE, 6, arm_mode, &response) == PLP_OK &&
              core.operating_mode == PLP_OPERATING_MODE_OFFLINE_ARMED &&
              core.offline_autorun_enabled == 1 && mode_store.generation == 1,
          "Firmware persists Offline Armed autorun mode over HID");

    std::array<std::uint8_t, 16> offline_job{};
    offline_job[0] = 0x77;
    Check(ProgrammerCore_StartOffline(&core, offline_job.data()) == PLP_OK &&
              core.operation_state == PLP_OPERATION_RUNNING,
          "Firmware offline job start");
    ProgrammerCore_Process(&core);
    ProgrammerCore_Process(&core);
    Check(core.operation_state == PLP_OPERATION_COMPLETED &&
              core.terminal_result == PLP_TERMINAL_PASS,
          "Firmware authoritative PASS terminal state");

    Check(CoreCommand(&core, PLP_CMD_GET_LAST_OFFLINE_RESULT, 7, {},
                      &response) == PLP_OK &&
              response.payload_length == PLP_COMMON_RESPONSE_SIZE + 44u &&
              response.payload[PLP_COMMON_RESPONSE_SIZE + 18u] ==
                  PLP_TERMINAL_PASS &&
              response.payload[PLP_COMMON_RESPONSE_SIZE + 31u] == 0u,
          "Firmware exposes the last offline PASS without a failure snapshot");

    target.fail = true;
    target.failure_snapshot.version = PLP_OFFLINE_FAILURE_SNAPSHOT_VERSION;
    target.failure_snapshot.valid = 1;
    target.failure_snapshot.protocol_step = 14;
    target.failure_snapshot.unit_byte_index = 2;
    target.failure_snapshot.tx_value = 0xA5;
    target.failure_snapshot.tx_bit_index = 3;
    target.failure_snapshot.timer_state = 1;
    target.failure_snapshot.pin_flags =
        PLP_OFFLINE_FAILURE_LINK_VALID |
        PLP_OFFLINE_FAILURE_DESTRUCTIVE |
        PLP_OFFLINE_FAILURE_PIN_PA10_INPUT;
    target.failure_snapshot.address = 0x00000504u;
    target.failure_snapshot.timer_count = 103;
    offline_job[0] = 0x78;
    offline_job[12] = 0x2A;
    Check(ProgrammerCore_StartOffline(&core, offline_job.data()) == PLP_OK,
          "Firmware starts a second offline job for diagnostics");
    ProgrammerCore_Process(&core);
    ProgrammerCore_Process(&core);
    Check(CoreCommand(&core, PLP_CMD_GET_LAST_OFFLINE_RESULT, 8, {},
                      &response) == PLP_OK &&
              response.payload[PLP_COMMON_RESPONSE_SIZE + 18u] ==
                  PLP_TERMINAL_FAIL &&
              Rpp_ReadLe32(response.payload + PLP_COMMON_RESPONSE_SIZE + 20u) ==
                  PLP_ERR_TARGET_DIRECTION &&
              Rpp_ReadLe16(response.payload + PLP_COMMON_RESPONSE_SIZE + 24u) ==
                  321u &&
              response.payload[PLP_COMMON_RESPONSE_SIZE + 31u] == 1u &&
              response.payload[PLP_COMMON_RESPONSE_SIZE + 32u] == 14u &&
              response.payload[PLP_COMMON_RESPONSE_SIZE + 34u] == 0xA5u &&
              Rpp_ReadLe32(response.payload + PLP_COMMON_RESPONSE_SIZE + 38u) ==
                  0x00000504u,
          "Firmware returns the compact offline failure snapshot over PLP");

    std::vector<std::uint8_t> failure_cursor(4, std::uint8_t{0});
    Check(CoreCommand(&core, PLP_CMD_GET_OFFLINE_FAILURE_HISTORY, 80,
                      failure_cursor, &response) == PLP_OK &&
              response.payload_length == PLP_COMMON_RESPONSE_SIZE + 44u &&
              (response.payload[PLP_COMMON_RESPONSE_SIZE + 10u] &
               PLP_OFFLINE_FAILURE_HISTORY_VALID) != 0u &&
              response.payload[PLP_COMMON_RESPONSE_SIZE + 11u] ==
                  PLP_OFFLINE_FAILURE_HISTORY_VERSION &&
              Rpp_ReadLe32(response.payload + PLP_COMMON_RESPONSE_SIZE) == 1u &&
              Rpp_ReadLe32(response.payload + PLP_COMMON_RESPONSE_SIZE + 12u) ==
                  42u &&
              Rpp_ReadLe32(response.payload + PLP_COMMON_RESPONSE_SIZE + 24u) ==
                  PLP_ERR_TARGET_DIRECTION &&
              Rpp_ReadLe32(response.payload + PLP_COMMON_RESPONSE_SIZE + 38u) ==
                  0x00000504u,
          "Firmware retains an indexed offline failure history record");
    target.fail = false;

    offline_job[0] = 0x79;
    offline_job[12] = 0x2B;
    Check(ProgrammerCore_StartOffline(&core, offline_job.data()) == PLP_OK,
          "Firmware starts a successful job after a retained failure");
    ProgrammerCore_Process(&core);
    ProgrammerCore_Process(&core);
    Check(CoreCommand(&core, PLP_CMD_GET_LAST_OFFLINE_RESULT, 81, {},
                      &response) == PLP_OK &&
              response.payload[PLP_COMMON_RESPONSE_SIZE + 18u] ==
                  PLP_TERMINAL_PASS,
          "Firmware last offline result advances to the later PASS");
    Check(CoreCommand(&core, PLP_CMD_GET_OFFLINE_FAILURE_HISTORY, 82,
                      failure_cursor, &response) == PLP_OK &&
              Rpp_ReadLe32(response.payload + PLP_COMMON_RESPONSE_SIZE) == 1u &&
              Rpp_ReadLe32(response.payload + PLP_COMMON_RESPONSE_SIZE + 12u) ==
                  42u &&
              Rpp_ReadLe32(response.payload + PLP_COMMON_RESPONSE_SIZE + 24u) ==
                  PLP_ERR_TARGET_DIRECTION,
          "Firmware PASS does not erase the retained failure history");

    target.fail = true;
    bool history_fill_passed = true;
    for (std::uint32_t index = 0;
         index < PROGRAMMER_OFFLINE_FAILURE_HISTORY_CAPACITY;
         ++index) {
        offline_job[12] = static_cast<std::uint8_t>(0x40u + index);
        if (ProgrammerCore_StartOffline(&core, offline_job.data()) != PLP_OK) {
            history_fill_passed = false;
            break;
        }
        ProgrammerCore_Process(&core);
        ProgrammerCore_Process(&core);
    }
    Check(history_fill_passed &&
              core.offline_failure_count ==
                  PROGRAMMER_OFFLINE_FAILURE_HISTORY_CAPACITY &&
              core.offline_failure_dropped == 1u,
          "Firmware failure history is bounded and reports overwrite");
    Check(CoreCommand(&core, PLP_CMD_GET_OFFLINE_FAILURE_HISTORY, 83,
                      failure_cursor, &response) == PLP_OK &&
              (response.payload[PLP_COMMON_RESPONSE_SIZE + 10u] &
               PLP_OFFLINE_FAILURE_HISTORY_OVERFLOW) != 0u &&
              Rpp_ReadLe16(response.payload + PLP_COMMON_RESPONSE_SIZE + 8u) ==
                  1u &&
              Rpp_ReadLe32(response.payload + PLP_COMMON_RESPONSE_SIZE) == 2u,
          "Firmware history query exposes dropped count and oldest sequence");
    target.fail = false;

    std::vector<std::uint8_t> online_mode(8, std::uint8_t{0});
    Rpp_WriteLe32(online_mode.data() + 4, mode_store.generation);
    Check(CoreCommand(&core, PLP_CMD_SET_OPERATING_MODE, 9, online_mode, &response) == PLP_OK &&
              core.operating_mode == PLP_OPERATING_MODE_ONLINE_ONLY &&
              mode_store.mode == PLP_OPERATING_MODE_ONLINE_ONLY,
          "Firmware switches back to Online Only without deleting package");

    const std::array<std::uint8_t, 16> session_uuid = [&core]() {
        std::array<std::uint8_t, 16> value{};
        std::copy_n(core.session_uuid, value.size(), value.begin());
        return value;
    }();
    std::vector<std::uint8_t> close_session(20, std::uint8_t{0});
    std::copy(session_uuid.begin(), session_uuid.end(), close_session.begin());
    Check(CoreCommand(&core, PLP_CMD_CLOSE_SESSION, 10, close_session, &response) == PLP_OK &&
              response.payload_length == PLP_COMMON_RESPONSE_SIZE + 16u &&
              std::equal(session_uuid.begin(), session_uuid.end(),
                         response.payload + PLP_COMMON_RESPONSE_SIZE) &&
              core.session_open == 0 && core.state == PLP_STATE_READY,
          "Firmware closes the exact PLP session before HID disconnect");
    core.negotiated = 1;
    core.session_open = 1;
    ProgrammerCore_OnTransportDisconnected(&core);
    Check(core.negotiated == 0 && core.session_open == 0 &&
              core.state == PLP_STATE_READY_UNNEGOTIATED,
          "Firmware clears stale session ownership on transport reset");
}

void TestFirmwareBootloaderEntry() {
    ProgrammerCoreConfig config{};
    ProgrammerCore core{};
    FakeBoot boot;
    RppPlpFrame response{};
    std::uint16_t sequence = 200;

    config.capabilities = PLP_CAP_PROGRAMMER_UPDATE;
    config.boot.context = &boot;
    config.boot.prepare = FakePrepareBoot;
    ProgrammerCore_Init(&core, &config);
    Check(PLP_CMD_FW_ENTER_BOOTLOADER == 0x0506u,
          "Firmware bootloader-entry command has dedicated PLP allocation");

    core.negotiated = 1;
    core.session_open = 1;
    core.state = PLP_STATE_SESSION;
    Check(CoreCommand(&core, PLP_CMD_FW_ENTER_BOOTLOADER, sequence++, {0x01},
                      &response) == PLP_ERR_BAD_LENGTH &&
              boot.prepare_calls == 0,
          "Firmware bootloader entry rejects payload before preparation");

    core.negotiated = 0;
    core.session_open = 0;
    Check(CoreCommand(&core, PLP_CMD_FW_ENTER_BOOTLOADER, sequence++, {},
                      &response) == PLP_ERR_INVALID_STATE &&
              boot.prepare_calls == 0,
          "Firmware bootloader entry requires negotiated open session");

    core.negotiated = 1;
    core.session_open = 1;
    core.state = PLP_STATE_SESSION;
    core.config.capabilities = 0;
    Check(CoreCommand(&core, PLP_CMD_FW_ENTER_BOOTLOADER, sequence++, {},
                      &response) == PLP_ERR_CAPABILITY_UNSUPPORTED &&
              boot.prepare_calls == 0,
          "Firmware bootloader entry requires advertised capability");

    core.config.capabilities = PLP_CAP_PROGRAMMER_UPDATE;
    core.config.boot.prepare = 0;
    Check(CoreCommand(&core, PLP_CMD_FW_ENTER_BOOTLOADER, sequence++, {},
                      &response) == PLP_ERR_CAPABILITY_UNSUPPORTED &&
              boot.prepare_calls == 0,
          "Firmware bootloader entry fails closed without prepare callback");

    core.config.boot.prepare = FakePrepareBoot;
    core.active_transfer_id = 1;
    Check(CoreCommand(&core, PLP_CMD_FW_ENTER_BOOTLOADER, sequence++, {},
                      &response) == PLP_ERR_PROGRAM_BUSY &&
              boot.prepare_calls == 0,
          "Firmware bootloader entry rejects active image transfer");

    core.active_transfer_id = 0;
    core.operation_state = PLP_OPERATION_RUNNING;
    core.state = PLP_STATE_PROGRAMMING;
    Check(CoreCommand(&core, PLP_CMD_FW_ENTER_BOOTLOADER, sequence++, {},
                      &response) == PLP_ERR_PROGRAM_BUSY &&
              boot.prepare_calls == 0,
          "Firmware bootloader entry rejects active programming job");

    core.operation_state = PLP_OPERATION_COMPLETED;
    core.state = PLP_STATE_VALIDATING_IMAGE;
    Check(CoreCommand(&core, PLP_CMD_FW_ENTER_BOOTLOADER, sequence++, {},
                      &response) == PLP_ERR_INVALID_STATE &&
              boot.prepare_calls == 0,
          "Firmware bootloader entry rejects destructive core state");

    core.state = PLP_STATE_SESSION;
    boot.result = PLP_ERR_UPDATE_APPLY;
    Check(CoreCommand(&core, PLP_CMD_FW_ENTER_BOOTLOADER, sequence++, {},
                      &response) == PLP_ERR_UPDATE_APPLY &&
              boot.prepare_calls == 1 && core.state == PLP_STATE_SESSION,
          "Firmware bootloader entry reports prepare failure without state change");

    boot.result = PLP_OK;
    Check(CoreCommand(&core, PLP_CMD_FW_ENTER_BOOTLOADER, sequence++, {},
                      &response) == PLP_OK &&
              boot.prepare_calls == 2 && core.state == PLP_STATE_UPDATING &&
              response.payload_length == PLP_COMMON_RESPONSE_SIZE &&
              response.payload[4] == PLP_STATE_UPDATING,
          "Firmware bootloader entry prepares once and ACKs updating state");
}

void TestFirmwareProgrammerReset() {
    ProgrammerCoreConfig config{};
    ProgrammerCore core{};
    FakeBoot boot;
    RppPlpFrame response{};
    std::uint16_t sequence = 240;

    config.capabilities = PLP_CAP_PROGRAMMER_RESET;
    config.boot.context = &boot;
    config.boot.prepare_reset = FakePrepareReset;
    ProgrammerCore_Init(&core, &config);
    Check(PLP_CMD_RESET_PROGRAMMER == 0x0007u &&
              PLP_CAP_PROGRAMMER_RESET == (1ul << 26),
          "Firmware programmer-reset command and capability have dedicated allocations");

    core.negotiated = 1;
    core.session_open = 1;
    core.state = PLP_STATE_SESSION;
    Check(CoreCommand(&core, PLP_CMD_RESET_PROGRAMMER, sequence++, {0x01},
                      &response) == PLP_ERR_BAD_LENGTH &&
              boot.reset_calls == 0,
          "Firmware programmer reset rejects payload before scheduling reset");

    core.negotiated = 0;
    core.session_open = 0;
    Check(CoreCommand(&core, PLP_CMD_RESET_PROGRAMMER, sequence++, {},
                      &response) == PLP_ERR_INVALID_STATE &&
              boot.reset_calls == 0,
          "Firmware programmer reset requires negotiated open session");

    core.negotiated = 1;
    core.session_open = 1;
    core.state = PLP_STATE_SESSION;
    core.config.capabilities = 0;
    Check(CoreCommand(&core, PLP_CMD_RESET_PROGRAMMER, sequence++, {},
                      &response) == PLP_ERR_CAPABILITY_UNSUPPORTED &&
              boot.reset_calls == 0,
          "Firmware programmer reset requires advertised capability");

    core.config.capabilities = PLP_CAP_PROGRAMMER_RESET;
    core.config.boot.prepare_reset = 0;
    Check(CoreCommand(&core, PLP_CMD_RESET_PROGRAMMER, sequence++, {},
                      &response) == PLP_ERR_CAPABILITY_UNSUPPORTED &&
              boot.reset_calls == 0,
          "Firmware programmer reset fails closed without reset callback");

    core.config.boot.prepare_reset = FakePrepareReset;
    core.active_transfer_id = 1;
    Check(CoreCommand(&core, PLP_CMD_RESET_PROGRAMMER, sequence++, {},
                      &response) == PLP_ERR_PROGRAM_BUSY &&
              boot.reset_calls == 0,
          "Firmware programmer reset rejects active image transfer");

    core.active_transfer_id = 0;
    core.operation_state = PLP_OPERATION_RUNNING;
    core.state = PLP_STATE_PROGRAMMING;
    Check(CoreCommand(&core, PLP_CMD_RESET_PROGRAMMER, sequence++, {},
                      &response) == PLP_ERR_PROGRAM_BUSY &&
              boot.reset_calls == 0,
          "Firmware programmer reset rejects active programming job");

    core.operation_state = PLP_OPERATION_FAILED;
    core.state = PLP_STATE_VALIDATING_IMAGE;
    Check(CoreCommand(&core, PLP_CMD_RESET_PROGRAMMER, sequence++, {},
                      &response) == PLP_ERR_INVALID_STATE &&
              boot.reset_calls == 0,
          "Firmware programmer reset rejects non-session core state");

    core.state = PLP_STATE_SESSION;
    boot.reset_result = PLP_ERR_UPDATE_APPLY;
    Check(CoreCommand(&core, PLP_CMD_RESET_PROGRAMMER, sequence++, {},
                      &response) == PLP_ERR_UPDATE_APPLY &&
              boot.reset_calls == 1 && core.state == PLP_STATE_SESSION,
          "Firmware programmer reset reports scheduling failure without state change");

    boot.reset_result = PLP_OK;
    Check(CoreCommand(&core, PLP_CMD_RESET_PROGRAMMER, sequence++, {},
                      &response) == PLP_OK &&
              boot.reset_calls == 2 && core.state == PLP_STATE_SESSION &&
              response.payload_length == PLP_COMMON_RESPONSE_SIZE &&
              response.payload[4] == PLP_STATE_SESSION,
          "Firmware programmer reset schedules once and ACKs before reset");
}

void TestM032Adapter() {
    M032Programmer programmer{};
    M032Programmer_Init(&programmer);
    const std::array<std::uint8_t, 16> default_programmer_uuid{
        0x4D, 0x30, 0x33, 0x32, 0x52, 0x4C, 0x37, 0x38,
        0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    const std::array<std::uint8_t, 16> default_profile_uuid{
        0x52, 0x4C, 0x37, 0x38, 0x47, 0x31, 0x55, 0x00,
        0x80, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x01};
    const std::array<std::uint8_t, 4> default_hardware_version{1, 0, 0, 0};
    const std::array<std::uint8_t, 8> default_firmware_version{1, 0, 1, 0,
                                                               2, 0, 0, 0};
    const std::string default_product = "M032 RL78 Programmer";
    const auto default_info = ReadDeviceInfo(&programmer.core);
    Check(DeviceInfoTlvEquals(default_info, 0x0001, default_programmer_uuid.data(),
                              default_programmer_uuid.size()) &&
              DeviceInfoTlvEquals(default_info, 0x0002,
                                  reinterpret_cast<const std::uint8_t*>(
                                      default_product.data()),
                                  default_product.size()) &&
              DeviceInfoTlvEquals(default_info, 0x0003,
                                  default_hardware_version.data(),
                                  default_hardware_version.size()) &&
              DeviceInfoTlvEquals(default_info, 0x0004,
                                  default_firmware_version.data(),
                                  default_firmware_version.size()) &&
              DeviceInfoTlvEquals(default_info, 0x0008, default_profile_uuid.data(),
                                  default_profile_uuid.size()),
          "M032 default identity is preserved through GET_DEVICE_INFO");

    M032Programmer custom_programmer{};
    M032ProgrammerIdentity custom_identity{};
    const std::string custom_product = "Nu-Link2-Me RL78 Programmer";
    const std::array<std::uint8_t, 16> custom_programmer_uuid{
        0x4E, 0x4C, 0x32, 0x4D, 0x52, 0x4C, 0x37, 0x38,
        0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02};
    const std::array<std::uint8_t, 16> custom_profile_uuid{
        0x4E, 0x4C, 0x32, 0x4D, 0x42, 0x4F, 0x41, 0x52,
        0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    const std::array<std::uint8_t, 4> custom_hardware_version{2, 1, 0, 0};
    const std::array<std::uint8_t, 8> custom_firmware_version{2, 0, 0, 0,
                                                              0, 0, 0, 3};
    custom_identity.product_name = custom_product.c_str();
    std::copy(custom_programmer_uuid.begin(), custom_programmer_uuid.end(),
              custom_identity.programmer_uuid);
    std::copy(custom_profile_uuid.begin(), custom_profile_uuid.end(),
              custom_identity.board_profile_uuid);
    std::copy(custom_hardware_version.begin(), custom_hardware_version.end(),
              custom_identity.hardware_version);
    std::copy(custom_firmware_version.begin(), custom_firmware_version.end(),
              custom_identity.firmware_version);
    M032Programmer_InitWithIdentity(&custom_programmer, &custom_identity);
    const auto custom_info = ReadDeviceInfo(&custom_programmer.core);
    Check(DeviceInfoTlvEquals(custom_info, 0x0001, custom_programmer_uuid.data(),
                              custom_programmer_uuid.size()) &&
              DeviceInfoTlvEquals(custom_info, 0x0002,
                                  reinterpret_cast<const std::uint8_t*>(
                                      custom_product.data()),
                                  custom_product.size()) &&
              DeviceInfoTlvEquals(custom_info, 0x0003,
                                  custom_hardware_version.data(),
                                  custom_hardware_version.size()) &&
              DeviceInfoTlvEquals(custom_info, 0x0004,
                                  custom_firmware_version.data(),
                                  custom_firmware_version.size()) &&
              DeviceInfoTlvEquals(custom_info, 0x0008, custom_profile_uuid.data(),
                                  custom_profile_uuid.size()),
          "Custom Nu-Link identity is reported through GET_DEVICE_INFO");

    std::array<std::uint8_t, PLP_REPORT_SIZE> request{};
    std::array<std::uint8_t, PLP_REPORT_SIZE> response{};
    std::array<std::uint8_t, 4> echo{0x12, 0x34, 0x56, 0x78};
    Check(Rpp_PlpEncode(1, 0, 0, PLP_CMD_PING, 1, echo.data(),
                        static_cast<std::uint16_t>(echo.size()), request.data()) == PLP_OK &&
              M032Programmer_OnOutReport(&programmer, request.data(),
                                          static_cast<std::uint32_t>(request.size())) == PLP_OK,
          "M032 HID OUT queue");
    M032Programmer_Process(&programmer);
    RppPlpFrame decoded{};
    Check(M032Programmer_TakeInReport(&programmer, response.data()) == 1 &&
              Rpp_PlpDecode(response.data(), &decoded) == PLP_OK &&
              Rpp_ReadLe32(decoded.payload) == PLP_OK &&
              std::equal(echo.begin(), echo.end(), decoded.payload + PLP_COMMON_RESPONSE_SIZE),
          "M032 HID IN response");
    Check(M032Programmer_OnOfflineButton(&programmer) == PLP_ERR_OFFLINE_NOT_ACTIVE,
          "M032 offline button fail-closed without storage");

    FakeTarget target;
    FakeModeStore mode_store;
    ProgrammerStorageOps storage{};
    ProgrammerTargetOps target_ops{};
    ProgrammerModeOps mode_ops{};
    storage.get_active = FakeGetActive;
    storage.open_image = FakeOpenImage;
    target_ops.context = &target;
    target_ops.profile_ready = 1;
    target_ops.start = FakeStart;
    target_ops.poll = FakePoll;
    target_ops.cancel = FakeCancel;
    target_ops.retry_connect = FakeRetryConnect;
    mode_store.mode = PLP_OPERATING_MODE_OFFLINE_ARMED;
    mode_store.autorun = 1;
    mode_store.generation = 3;
    mode_ops.context = &mode_store;
    mode_ops.load = FakeModeLoad;
    mode_ops.store = FakeModeWrite;
    M032Programmer_Configure(&programmer, &storage, &target_ops,
                             PLP_CAP_OFFLINE_PACKAGE | PLP_CAP_OFFLINE_AUTORUN |
                             PLP_CAP_TARGET_PROGRAM, nullptr, nullptr);
    Check(M032Programmer_ConfigureMode(&programmer, &mode_ops, 0) == PLP_OK &&
              M032Programmer_ProcessOfflineAutorun(&programmer, 1000, 1000) == PLP_OK &&
              programmer.core.operation_state == PLP_OPERATION_COMPLETED,
          "M032 power-on reset does not start offline programming");
    Check(M032Programmer_ConfigureMode(&programmer, &mode_ops, 1) == PLP_OK &&
              M032Programmer_ProcessOfflineAutorun(&programmer, 999, 1000) == PLP_OK &&
              programmer.core.operation_state == PLP_OPERATION_COMPLETED,
          "M032 offline autorun observes HID grace period");
    Check(M032Programmer_ProcessOfflineAutorun(&programmer, 1000, 1000) == PLP_OK &&
              programmer.core.operation_state == PLP_OPERATION_RUNNING,
          "M032 offline autorun starts active package without HID session");
    programmer.core.session_open = 1;
    M032Programmer_Process(&programmer);
    M032Programmer_Process(&programmer);
    Check(programmer.autorun_suppressed == 1 &&
              programmer.core.operation_state == PLP_OPERATION_CANCELLED,
          "M032 HID session safely suppresses offline handshake retry");

    M032Programmer host_programmer{};
    FakeTarget host_target;
    M032Programmer_Init(&host_programmer);
    target_ops.context = &host_target;
    M032Programmer_Configure(&host_programmer, &storage, &target_ops,
                             PLP_CAP_OFFLINE_PACKAGE | PLP_CAP_OFFLINE_BUTTON |
                             PLP_CAP_TARGET_PROGRAM, nullptr, nullptr);
    mode_store.autorun = 0;
    Check(M032Programmer_ConfigureMode(&host_programmer, &mode_ops, 0) == PLP_OK,
          "M032 manual offline-button mode loads without autorun");
    host_programmer.core.negotiated = 1;
    host_programmer.core.session_open = 1;
    host_programmer.core.state = PLP_STATE_SESSION;
    Check(M032Programmer_OnOfflineButton(&host_programmer) == PLP_OK &&
              host_programmer.core.session_open == 0 &&
              host_programmer.autonomous_offline_active == 1,
          "Physical offline button releases a stale idle HID session");
    M032Programmer_Process(&host_programmer);
    Check(M032Programmer_OnOfflineButton(&host_programmer) == PLP_OK &&
              host_target.retry_connect_calls == 1 &&
              host_target.start_calls == 1 &&
              host_programmer.core.operation_state == PLP_OPERATION_RUNNING,
          "Physical offline button immediately retries target handshake");
    host_programmer.core.job_stage = 8;
    Check(M032Programmer_OnOfflineButton(&host_programmer) == PLP_ERR_PROGRAM_BUSY &&
              host_target.retry_connect_calls == 1,
          "Physical offline button cannot restart a destructive stage");

    M032Programmer host_active_package{};
    FakeTarget host_active_target;
    std::array<std::uint8_t, 16> host_job{};
    M032Programmer_Init(&host_active_package);
    target_ops.context = &host_active_target;
    M032Programmer_Configure(&host_active_package, &storage, &target_ops,
                             PLP_CAP_OFFLINE_PACKAGE | PLP_CAP_TARGET_PROGRAM,
                             nullptr, nullptr);
    Check(M032Programmer_ConfigureMode(&host_active_package, &mode_ops, 0) == PLP_OK,
          "M032 host active-package mode loads");
    host_active_package.core.session_open = 1;
    host_job[0] = 0x51;
    Check(ProgrammerCore_StartOffline(&host_active_package.core,
                                      host_job.data()) == PLP_OK,
          "Host starts an active-package programming job");
    M032Programmer_Process(&host_active_package);
    Check(host_active_target.cancel == false &&
              host_active_package.core.operation_state == PLP_OPERATION_RUNNING &&
              host_active_package.autonomous_offline_active == 0,
          "Host active-package job is not mistaken for KEY1 autonomous work");
}

struct MemoryStorage {
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(65u * 1024u, 0xFFu);
    std::uint32_t erase_size = 512;
    std::uint32_t read_calls = 0;
    std::uint64_t read_bytes = 0;
};

std::uint32_t MemoryRead(void* context, std::uint32_t offset,
                         std::uint8_t* data, std::uint32_t length) {
    auto* memory = static_cast<MemoryStorage*>(context);
    if (data == nullptr || offset > memory->bytes.size() ||
        length > memory->bytes.size() - offset) return PLP_ERR_STORAGE_OFFSET;
    ++memory->read_calls;
    memory->read_bytes += length;
    std::copy_n(memory->bytes.begin() + offset, length, data);
    return PLP_OK;
}

std::uint32_t MemoryErase(void* context, std::uint32_t offset, std::uint32_t length) {
    auto* memory = static_cast<MemoryStorage*>(context);
    if (offset % memory->erase_size != 0 || length % memory->erase_size != 0 ||
        offset > memory->bytes.size() || length > memory->bytes.size() - offset) {
        return PLP_ERR_STORAGE_OFFSET;
    }
    std::fill_n(memory->bytes.begin() + offset, length, std::uint8_t{0xFF});
    return PLP_OK;
}

std::uint32_t MemoryWrite(void* context, std::uint32_t offset,
                          const std::uint8_t* data, std::uint32_t length) {
    auto* memory = static_cast<MemoryStorage*>(context);
    if (data == nullptr || offset > memory->bytes.size() ||
        length > memory->bytes.size() - offset) return PLP_ERR_STORAGE_OFFSET;
    for (std::uint32_t i = 0; i < length; ++i) {
        if ((memory->bytes[offset + i] & data[i]) != data[i]) return PLP_ERR_STORAGE_WRITE;
        memory->bytes[offset + i] &= data[i];
    }
    return PLP_OK;
}

std::uint32_t MemorySync(void*) {
    return PLP_OK;
}

void TestM032PackageStore() {
    MemoryStorage memory;
    M032StorageBackend backend{};
    M032PackageStore store{};
    ProgrammerStorageOps storage{};
    ProgrammerModeOps mode{};
    backend.context = &memory;
    backend.capacity = static_cast<std::uint32_t>(memory.bytes.size());
    backend.erase_size = memory.erase_size;
    backend.read = MemoryRead;
    backend.erase = MemoryErase;
    backend.write = MemoryWrite;
    backend.sync = MemorySync;
    Check(M032PackageStore_Init(&store, &backend) == PLP_OK &&
              store.slot_offset == 1024u &&
              store.slot_capacity == 64u * 1024u,
          "M032 package store reserves two journal pages and one 64 KiB slot");
    M032PackageStore_GetStorageOps(&store, &storage);
    M032PackageStore_GetModeOps(&store, &mode);

    rl78_programmer::OipBuildOptions options;
    options.build_time_ms = 123456789u;
    const auto package = rl78_programmer::BuildOfflineImagePackage(BuildTestImage(), options);
    std::uint16_t transfer_id = 0;
    std::uint32_t accepted = 0;
    Check(storage.begin(storage.context, package.package_uuid.data(),
                        64u * 1024u + 1u, package.crc32,
                        package.sha256.data(), &transfer_id, &accepted) ==
              PLP_ERR_STORAGE_CAPACITY,
          "M032 package store rejects packages above the 64 KiB limit");
    Check(storage.begin(storage.context, package.package_uuid.data(),
                        static_cast<std::uint32_t>(package.bytes.size()), package.crc32,
                        package.sha256.data(), &transfer_id, &accepted) == PLP_OK,
          "M032 package store reserves slot A");
    while (accepted < package.bytes.size()) {
        const auto length = static_cast<std::uint8_t>((std::min<std::size_t>)(
            45u, package.bytes.size() - accepted));
        const auto status = storage.write(storage.context, transfer_id, accepted,
                                          package.bytes.data() + accepted, length, &accepted);
        if (status != PLP_OK) {
            Check(false, "M032 package store streams package chunks");
            break;
        }
    }
    Check(accepted == package.bytes.size() &&
              storage.finalize(storage.context, transfer_id,
                               static_cast<std::uint32_t>(package.bytes.size()),
                               package.sha256.data(), package.crc32) == PLP_OK,
          "M032 package store validates SHA-256 CRC and OIP header");
    std::uint32_t activation_generation = 0;
    Check(storage.activate(storage.context, package.package_uuid.data(), 0,
                           &activation_generation) == PLP_OK && activation_generation == 1,
          "M032 package store atomically activates validated image");
    std::array<std::uint8_t, 4> magic{};
    Check(M032PackageStore_ReadActive(&store, 0, magic.data(),
                                      static_cast<std::uint32_t>(magic.size())) == PLP_OK &&
              magic[0] == RPP_OIP_MAGIC_0 && magic[3] == RPP_OIP_MAGIC_3,
          "M032 active image provider reads the selected slot");
    ProgrammerImageOps opened_image{};
    std::array<std::uint8_t, 6> target_bytes{};
    Check(storage.open_image(storage.context, 1, package.package_uuid.data(),
                             &opened_image) == PLP_OK &&
              opened_image.read_target(opened_image.context, 0, target_bytes.data(),
                                       static_cast<std::uint32_t>(target_bytes.size())) == PLP_OK &&
              target_bytes == std::array<std::uint8_t, 6>{0x34, 0x12, 0x00,
                                                          0x01, 0x02, 0x03} &&
              opened_image.has_region(opened_image.context,
                                      RPP_OIP_SECTION_DATA_FLASH) == 1,
          "M032 image provider maps OIP sections into target address space");

    std::uint32_t mode_generation = 0;
    Check(mode.store(mode.context, PLP_OPERATING_MODE_OFFLINE_ARMED, 1, 0,
                     &mode_generation) == PLP_OK && mode_generation == 1,
          "M032 package store journals Offline Armed mode");
    M032PackageStore recovered{};
    ProgrammerStorageOps recovered_storage{};
    ProgrammerModeOps recovered_mode{};
    Check(M032PackageStore_Init(&recovered, &backend) == PLP_OK,
          "M032 package store recovers latest valid journal after reboot");
    M032PackageStore_GetStorageOps(&recovered, &recovered_storage);
    M032PackageStore_GetModeOps(&recovered, &recovered_mode);
    std::array<std::uint8_t, 16> active_uuid{};
    std::uint32_t recovered_activation = 0;
    std::uint8_t recovered_mode_value = 0;
    std::uint8_t recovered_autorun = 0;
    std::uint32_t recovered_mode_generation = 0;
    Check(recovered_storage.get_active(recovered_storage.context, active_uuid.data(),
                                       &recovered_activation) == PLP_OK &&
              active_uuid == package.package_uuid && recovered_activation == 1 &&
              recovered_mode.load(recovered_mode.context, &recovered_mode_value,
                                  &recovered_autorun,
                                  &recovered_mode_generation) == PLP_OK &&
              recovered_mode_value == PLP_OPERATING_MODE_OFFLINE_ARMED &&
              recovered_autorun == 1 && recovered_mode_generation == 1,
          "M032 package and operating mode survive power cycle");

    std::uint16_t replacement_transfer_id = 0;
    std::uint32_t replacement_offset = 0;
    Check(recovered_storage.begin(recovered_storage.context,
                                  package.package_uuid.data(),
                                  static_cast<std::uint32_t>(package.bytes.size()),
                                  package.crc32, package.sha256.data(),
                                  &replacement_transfer_id,
                                  &replacement_offset) == PLP_OK,
          "M032 single-slot replacement starts after invalidating active image");
    M032PackageStore interrupted{};
    ProgrammerStorageOps interrupted_storage{};
    Check(M032PackageStore_Init(&interrupted, &backend) == PLP_OK,
          "M032 package store recovers after replacement power loss");
    M032PackageStore_GetStorageOps(&interrupted, &interrupted_storage);
    Check(interrupted_storage.get_active(interrupted_storage.context,
                                         active_uuid.data(),
                                         &recovered_activation) ==
              PLP_ERR_STORAGE_NO_IMAGE,
          "M032 interrupted single-slot replacement cannot expose partial image");
}

void TestNulink2MeSpimGeometry() {
    MemoryStorage memory;
    memory.bytes.assign(NULINK2ME_W25Q16_CAPACITY, std::uint8_t{0xFF});
    memory.erase_size = NULINK2ME_W25Q16_ERASE_SIZE;
    M032StorageBackend backend{};
    backend.context = &memory;
    backend.capacity = static_cast<std::uint32_t>(memory.bytes.size());
    backend.erase_size = memory.erase_size;
    backend.read = MemoryRead;
    backend.erase = MemoryErase;
    backend.write = MemoryWrite;
    backend.sync = MemorySync;
    M032PackageStore store{};
    Check(M032PackageStore_Init(&store, &backend) == PLP_OK &&
              store.slot_offset == 2u * NULINK2ME_W25Q16_ERASE_SIZE &&
              store.slot_capacity == NULINK2ME_W25Q16_CAPACITY -
                                         2u * NULINK2ME_W25Q16_ERASE_SIZE,
          "Nu-Link2-Me W25Q16 reserves two journals and one geometry-sized slot");
}

void TestNulink2MeImageCache() {
    MemoryStorage memory;
    memory.bytes.assign(NULINK2ME_W25Q16_CAPACITY, std::uint8_t{0xFF});
    memory.erase_size = NULINK2ME_W25Q16_ERASE_SIZE;
    M032StorageBackend backend{};
    backend.context = &memory;
    backend.capacity = static_cast<std::uint32_t>(memory.bytes.size());
    backend.erase_size = memory.erase_size;
    backend.read = MemoryRead;
    backend.erase = MemoryErase;
    backend.write = MemoryWrite;
    backend.sync = MemorySync;
    M032PackageStore store{};
    ProgrammerStorageOps backing{};
    Check(M032PackageStore_Init(&store, &backend) == PLP_OK,
          "Nu-Link2-Me image cache initializes its W25Q16 package store");
    M032PackageStore_GetStorageOps(&store, &backing);

    std::vector<std::uint8_t> cache_bytes(NULINK2ME_IMAGE_CACHE_CAPACITY, 0u);
    Nulink2MeImageCache cache{};
    ProgrammerStorageOps storage{};
    Check(Nulink2MeImageCache_Init(
              &cache, &backing, cache_bytes.data(),
              static_cast<std::uint32_t>(cache_bytes.size()),
              Rl78ProtocolB_GetProfileCodeSize) == PLP_OK,
          "Nu-Link2-Me image cache accepts a 64 KiB global-buffer equivalent");
    Nulink2MeImageCache_GetStorageOps(&cache, &storage);

    rl78_programmer::OipBuildOptions options;
    options.target_profile = rl78_programmer::Rl78G15_8KTargetProfile();
    const auto package = rl78_programmer::BuildOfflineImagePackage(
        BuildTestImage(), options);
    std::uint16_t transfer_id = 0;
    std::uint32_t accepted = 0;
    bool transfer_ok = storage.begin(
        storage.context, package.package_uuid.data(),
        static_cast<std::uint32_t>(package.bytes.size()), package.crc32,
        package.sha256.data(), &transfer_id, &accepted) == PLP_OK;
    while (transfer_ok && accepted < package.bytes.size()) {
        const auto length = static_cast<std::uint8_t>((std::min<std::size_t>)(
            45u, package.bytes.size() - accepted));
        transfer_ok = storage.write(
            storage.context, transfer_id, accepted,
            package.bytes.data() + accepted, length, &accepted) == PLP_OK;
    }
    transfer_ok = transfer_ok &&
        storage.finalize(storage.context, transfer_id,
                         static_cast<std::uint32_t>(package.bytes.size()),
                         package.sha256.data(), package.crc32) == PLP_OK;
    std::uint32_t activation_generation = 0;
    transfer_ok = transfer_ok &&
        storage.activate(storage.context, package.package_uuid.data(), 0,
                         &activation_generation) == PLP_OK;
    Check(transfer_ok && cache.valid == 0u,
          "Package download and activation leave stale SRAM invalidated");

    memory.read_calls = 0;
    memory.read_bytes = 0;
    Check(Nulink2MeImageCache_PreloadActive(&cache) == PLP_OK &&
              cache.valid != 0u && cache.image_size == 8u * 1024u &&
              cache.image_crc32 == Rpp_Crc32IsoHdlc(
                  cache_bytes.data(), cache.image_size) &&
              memory.read_calls > 0u,
          "Offline Armed boot preload expands the active 8 KiB image once");

    ProgrammerImageOps image{};
    Check(storage.open_image(storage.context, 1,
                             package.package_uuid.data(), &image) == PLP_OK,
          "Cached active image reopens after UUID/profile/CRC validation");
    const auto reads_after_open = memory.read_calls;
    std::array<std::uint8_t, 4> word{};
    bool sram_only = true;
    for (std::uint32_t address = 0; address < 8u * 1024u; address += 4u) {
        if (image.read_target(image.context, address, word.data(),
                              static_cast<std::uint32_t>(word.size())) != PLP_OK) {
            sram_only = false;
            break;
        }
    }
    Check(sram_only && memory.read_calls == reads_after_open,
          "All 2,048 programming units read SRAM without W25Q16 traffic");

    cache_bytes[0] ^= 0x01u;
    const auto reads_before_recovery = memory.read_calls;
    Check(storage.open_image(storage.context, 1,
                             package.package_uuid.data(), &image) == PLP_OK &&
              memory.read_calls > reads_before_recovery &&
              cache_bytes[0] == 0x34u,
          "SRAM CRC mismatch reloads the authoritative W25Q16 image before target access");

    std::uint32_t profile_size = 0;
    Check(Rl78ProtocolB_GetProfileCodeSize(
              package.target_profile_uuid.data(), &profile_size) == PLP_OK &&
              profile_size == 8u * 1024u,
          "Protocol B exposes the exact profile size to the cache boundary");
}

void TestNulink2MeOnlineOfflineSoftwareFlow() {
    MemoryStorage memory;
    memory.bytes.assign(NULINK2ME_W25Q16_CAPACITY, std::uint8_t{0xFF});
    memory.erase_size = NULINK2ME_W25Q16_ERASE_SIZE;
    M032StorageBackend backend{};
    backend.context = &memory;
    backend.capacity = static_cast<std::uint32_t>(memory.bytes.size());
    backend.erase_size = memory.erase_size;
    backend.read = MemoryRead;
    backend.erase = MemoryErase;
    backend.write = MemoryWrite;
    backend.sync = MemorySync;
    M032PackageStore store{};
    ProgrammerStorageOps storage{};
    ProgrammerModeOps mode{};
    Check(M032PackageStore_Init(&store, &backend) == PLP_OK,
          "Nu-Link2-Me composed flow initializes W25Q16 package store");
    M032PackageStore_GetStorageOps(&store, &storage);
    M032PackageStore_GetModeOps(&store, &mode);

    rl78_programmer::OipBuildOptions options;
    options.target_profile = rl78_programmer::Rl78G15_8KTargetProfile();
    const auto package = rl78_programmer::BuildOfflineImagePackage(
        BuildTestImage(), options);
    std::uint16_t transfer_id = 0;
    std::uint32_t accepted = 0;
    bool transfer_ok = storage.begin(
        storage.context, package.package_uuid.data(),
        static_cast<std::uint32_t>(package.bytes.size()), package.crc32,
        package.sha256.data(), &transfer_id, &accepted) == PLP_OK;
    while (transfer_ok && accepted < package.bytes.size()) {
        const auto length = static_cast<std::uint8_t>((std::min<std::size_t>)(
            45u, package.bytes.size() - accepted));
        transfer_ok = storage.write(
            storage.context, transfer_id, accepted,
            package.bytes.data() + accepted, length, &accepted) == PLP_OK;
    }
    transfer_ok = transfer_ok &&
        storage.finalize(storage.context, transfer_id,
                         static_cast<std::uint32_t>(package.bytes.size()),
                         package.sha256.data(), package.crc32) == PLP_OK;
    Check(transfer_ok,
          "GUI-equivalent download validates an OIP candidate in W25Q16");

    FakeTarget target;
    ProgrammerTargetOps target_ops{};
    target_ops.context = &target;
    target_ops.profile_ready = 1;
    target_ops.start = FakeStart;
    target_ops.poll = FakePoll;
    target_ops.cancel = FakeCancel;
    M032Programmer programmer{};
    M032Programmer_Init(&programmer);
    M032Programmer_Configure(
        &programmer, &storage, &target_ops,
        PLP_CAP_OFFLINE_PACKAGE | PLP_CAP_OFFLINE_BUTTON |
            PLP_CAP_TARGET_PROGRAM,
        nullptr, nullptr);
    Check(M032Programmer_ConfigureMode(&programmer, &mode, 0) == PLP_OK,
          "Nu-Link2-Me composed flow loads persisted operating mode");

    std::vector<std::uint8_t> start_online(52, std::uint8_t{0});
    start_online[0] = 0x31;
    start_online[16] = 0;
    std::copy(package.package_uuid.begin(), package.package_uuid.end(),
              start_online.begin() + 20);
    std::copy(package.target_profile_uuid.begin(), package.target_profile_uuid.end(),
              start_online.begin() + 36);
    RppPlpFrame response{};
    Check(CoreCommand(&programmer.core, PLP_CMD_PROGRAM_START, 50,
                      start_online, &response) == PLP_OK &&
              target.source_kind == 0,
          "Program Online opens the validated W25Q16 candidate");
    M032Programmer_Process(&programmer);
    M032Programmer_Process(&programmer);
    Check(programmer.core.operation_state == PLP_OPERATION_COMPLETED &&
              programmer.core.terminal_result == PLP_TERMINAL_PASS,
          "Program Online reaches authoritative PASS through target driver");

    std::uint32_t activation_generation = 0;
    std::uint32_t mode_generation = 0;
    Check(storage.activate(storage.context, package.package_uuid.data(), 0,
                           &activation_generation) == PLP_OK &&
              mode.store(mode.context, PLP_OPERATING_MODE_OFFLINE_ARMED,
                         0, 0, &mode_generation) == PLP_OK &&
              M032Programmer_ConfigureMode(&programmer, &mode, 0) == PLP_OK,
          "GUI-equivalent Activate and Arm persist the KEY1 package");
    programmer.core.negotiated = 1;
    programmer.core.session_open = 1;
    programmer.core.state = PLP_STATE_SESSION;
    Check(M032Programmer_OnOfflineButton(&programmer) == PLP_OK &&
              programmer.core.session_open == 0 &&
              target.source_kind == 1,
          "KEY1 opens the active W25Q16 package without a HID session");
    M032Programmer_Process(&programmer);
    M032Programmer_Process(&programmer);
    Check(programmer.core.operation_state == PLP_OPERATION_COMPLETED &&
              programmer.core.terminal_result == PLP_TERMINAL_PASS &&
              target.start_calls == 2,
          "KEY1 offline programming reaches authoritative PASS through target driver");
}

struct ProtocolImage {
    bool has_data_flash = false;
    bool return_erased = false;
};

struct ProtocolTransport {
    std::deque<std::uint8_t> receive;
    std::vector<std::uint8_t> transmit;
    std::uint32_t now_ms = 0;
    std::uint32_t code_size = 4u * 1024u;
    std::uint32_t programmed = 0;
    std::uint16_t enter_calls = 0;
    std::uint16_t prepare_calls = 0;
    std::uint16_t leave_calls = 0;
    std::uint16_t restart_calls = 0;
    std::uint8_t size_code = 0x0F;
    std::uint8_t respond_after_enter = 1;
    std::uint8_t erase_status = 0x06;
    std::uint8_t unit_status = 0x06;
    std::uint8_t verify_status = 0x06;
    std::uint8_t phase = 0;
    bool suppress_erase_status = false;
    std::uint32_t fail_after_programmed = 0;
    ProgrammerTargetFailureSnapshot failure_snapshot{};
    ProtocolImage* corrupt_image_after_preflight = nullptr;
};

std::uint32_t ProtocolEnter(void* context) {
    auto* transport = static_cast<ProtocolTransport*>(context);
    ++transport->enter_calls;
    transport->phase = 0;
    transport->programmed = 0;
    transport->receive.clear();
    return PLP_OK;
}

std::uint32_t ProtocolPrepare(void* context) {
    auto* transport = static_cast<ProtocolTransport*>(context);
    ++transport->prepare_calls;
    if (transport->corrupt_image_after_preflight != nullptr) {
        transport->corrupt_image_after_preflight->return_erased = true;
    }
    return PLP_OK;
}

std::uint32_t ProtocolLeave(void* context) {
    auto* transport = static_cast<ProtocolTransport*>(context);
    ++transport->leave_calls;
    transport->phase = 0;
    transport->receive.clear();
    return PLP_OK;
}

std::uint32_t ProtocolRestart(void* context) {
    ++static_cast<ProtocolTransport*>(context)->restart_calls;
    return PLP_OK;
}

std::uint32_t ProtocolWrite(void* context, std::uint8_t value) {
    auto* transport = static_cast<ProtocolTransport*>(context);
    if (transport->phase == 3 && transport->fail_after_programmed != 0u &&
        transport->programmed >= transport->fail_after_programmed) {
        return PLP_ERR_TARGET_DIRECTION;
    }
    transport->transmit.push_back(value);
    if (transport->phase == 0 && value == 0x3A) {
        transport->phase = 1;
        if (transport->enter_calls >= transport->respond_after_enter) {
            transport->receive.push_back(0x36);
        }
    } else if (transport->phase == 1 && value == 0x60) {
        transport->phase = 2;
        transport->receive.push_back(0x06);
        transport->receive.push_back(transport->size_code);
    } else if (transport->phase == 2 && value == 0x06) {
        transport->phase = 3;
        if (!transport->suppress_erase_status) {
            transport->receive.push_back(transport->erase_status);
        }
    } else if (transport->phase == 3) {
        ++transport->programmed;
        if ((transport->programmed % 4u) == 0u) {
            transport->receive.push_back(transport->unit_status);
        }
        if (transport->programmed == transport->code_size) {
            transport->receive.push_back(transport->verify_status);
            transport->phase = 4;
        }
    }
    return PLP_OK;
}

std::uint32_t ProtocolRead(void* context, std::uint8_t* value) {
    auto* transport = static_cast<ProtocolTransport*>(context);
    if (transport->receive.empty()) return PLP_ERR_SYSTEM_RESOURCE;
    *value = transport->receive.front();
    transport->receive.pop_front();
    return PLP_OK;
}

std::uint8_t ProtocolTransmitIdle(void*) {
    return 1;
}

std::uint32_t ProtocolNow(void* context) {
    return static_cast<ProtocolTransport*>(context)->now_ms;
}

std::uint32_t ProtocolGetFailureSnapshot(
    void* context, ProgrammerTargetFailureSnapshot* snapshot) {
    if (snapshot == nullptr) return PLP_ERR_SDK_ARGUMENT;
    *snapshot = static_cast<ProtocolTransport*>(context)->failure_snapshot;
    return PLP_OK;
}

std::uint32_t ProtocolImageRead(void* context, std::uint32_t address,
                                std::uint8_t* data, std::uint32_t length) {
    const auto* image = static_cast<const ProtocolImage*>(context);
    for (std::uint32_t index = 0; index < length; ++index) {
        data[index] = image->return_erased
            ? std::uint8_t{0xFF}
            : static_cast<std::uint8_t>(address + index);
    }
    return PLP_OK;
}

std::uint8_t ProtocolImageHasRegion(void* context, std::uint16_t type) {
    const auto* image = static_cast<ProtocolImage*>(context);
    return type == RPP_OIP_SECTION_DATA_FLASH && image->has_data_flash ? 1 : 0;
}

ProgrammerImageOps MakeProtocolImage(ProtocolImage* context,
                                     const rl78_programmer::TargetProfile& profile) {
    ProgrammerImageOps image{};
    image.context = context;
    std::copy(profile.uuid.begin(), profile.uuid.end(), image.target_profile_uuid);
    image.read_target = ProtocolImageRead;
    image.has_region = ProtocolImageHasRegion;
    return image;
}

Rl78ProtocolBTransportOps MakeProtocolTransport(ProtocolTransport* context) {
    Rl78ProtocolBTransportOps ops{};
    ops.context = context;
    ops.enter_programming_mode = ProtocolEnter;
    ops.prepare_attempt = ProtocolPrepare;
    ops.leave_safe = ProtocolLeave;
    ops.restart_target = ProtocolRestart;
    ops.try_write_byte = ProtocolWrite;
    ops.try_read_byte = ProtocolRead;
    ops.is_transmit_idle = ProtocolTransmitIdle;
    ops.now_ms = ProtocolNow;
    ops.get_failure_snapshot = ProtocolGetFailureSnapshot;
    return ops;
}

ProgrammerTargetStatus RunProtocolToTerminal(ProgrammerTargetOps* target,
                                             ProtocolTransport* transport) {
    ProgrammerTargetStatus status{};
    for (int poll = 0; poll < 80000 && status.terminal == 0; ++poll) {
        ++transport->now_ms;
        if (target->poll(target->context, &status) != PLP_OK) break;
    }
    return status;
}

void TestRl78ProtocolB() {
    ProtocolTransport transport;
    ProtocolImage image_context;
    Rl78ProtocolB driver{};
    ProgrammerTargetOps target{};
    auto transport_ops = MakeProtocolTransport(&transport);
    const auto& profile = rl78_programmer::Rl78G15_4KTargetProfile();
    auto image = MakeProtocolImage(&image_context, profile);
    Check(Rl78ProtocolB_Init(&driver, &transport_ops) == PLP_OK,
          "Protocol B transport initializes");
    Rl78ProtocolB_GetTargetOps(&driver, &target);
    Check(target.start(target.context, &image, 0, image.target_profile_uuid,
                       profile.uuid.data(), 0) == PLP_OK,
          "Protocol B accepts G15 4 KiB profile");
    auto status = RunProtocolToTerminal(&target, &transport);
    Check(status.terminal != 0 && status.terminal_result == PLP_TERMINAL_PASS &&
               status.stage == 14 && status.progress_basis_points == 10000 &&
               transport.programmed == 4u * 1024u && transport.restart_calls == 1 &&
               transport.prepare_calls == 1,
           "Protocol B completes mode/erase/program/internal-verify/reset flow");

    ProtocolTransport changed_image_transport;
    ProtocolImage changed_image_context;
    changed_image_transport.corrupt_image_after_preflight =
        &changed_image_context;
    Rl78ProtocolB changed_image_driver{};
    ProgrammerTargetOps changed_image_target{};
    auto changed_image_ops =
        MakeProtocolTransport(&changed_image_transport);
    auto changed_image =
        MakeProtocolImage(&changed_image_context, profile);
    Check(Rl78ProtocolB_Init(&changed_image_driver,
                            &changed_image_ops) == PLP_OK,
          "Protocol B image-integrity transport initializes");
    Rl78ProtocolB_GetTargetOps(&changed_image_driver,
                               &changed_image_target);
    Check(changed_image_target.start(
              changed_image_target.context, &changed_image, 0,
              changed_image.target_profile_uuid,
              profile.uuid.data(), 0) == PLP_OK,
          "Protocol B records the pre-entry dense image CRC");
    status = RunProtocolToTerminal(&changed_image_target,
                                   &changed_image_transport);
    Check(status.terminal_result == PLP_TERMINAL_FAIL &&
              status.error == PLP_ERR_INTEGRITY_CRC &&
              status.stage == 8 &&
              changed_image_transport.programmed == 4u * 1024u &&
              changed_image_transport.restart_calls == 1,
          "Protocol B rejects a changed transmitted image instead of false PASS");

    ProtocolTransport no_delay_transport;
    Rl78ProtocolB no_delay_driver{};
    ProgrammerTargetOps no_delay_target{};
    auto no_delay_ops = MakeProtocolTransport(&no_delay_transport);
    Check(Rl78ProtocolB_Init(&no_delay_driver, &no_delay_ops) == PLP_OK,
          "Protocol B no-delay transport initializes");
    Rl78ProtocolB_GetTargetOps(&no_delay_driver, &no_delay_target);
    Check(no_delay_target.start(no_delay_target.context, &image, 0,
                              image.target_profile_uuid,
                              profile.uuid.data(), 0) == PLP_OK,
          "Protocol B starts no-delay scenario");
    ProgrammerTargetStatus no_delay_status{};
    for (int poll = 0; poll < 100 && no_delay_transport.programmed < 4u; ++poll) {
        ++no_delay_transport.now_ms;
        static_cast<void>(no_delay_target.poll(no_delay_target.context,
                                               &no_delay_status));
    }
    const auto no_delay_start_ms = no_delay_transport.now_ms;
    for (int poll = 0; poll < 10; ++poll) {
        static_cast<void>(no_delay_target.poll(no_delay_target.context,
                                               &no_delay_status));
    }
    Check(no_delay_transport.now_ms == no_delay_start_ms &&
              no_delay_transport.programmed > 4u,
          "Protocol B starts the next SRAM unit without a millisecond guard");
    status = RunProtocolToTerminal(&no_delay_target, &no_delay_transport);
    Check(status.terminal_result == PLP_TERMINAL_PASS,
          "Protocol B no-delay scenario reaches PASS");

    ProtocolTransport g16_transport;
    g16_transport.code_size = 32u * 1024u;
    g16_transport.size_code = 0x7F;
    Rl78ProtocolB g16_driver{};
    ProgrammerTargetOps g16_target{};
    auto g16_ops = MakeProtocolTransport(&g16_transport);
    const auto& g16_profile = rl78_programmer::Rl78G16_32KTargetProfile();
    auto g16_image = MakeProtocolImage(&image_context, g16_profile);
    Check(Rl78ProtocolB_Init(&g16_driver, &g16_ops) == PLP_OK,
          "Protocol B G16 transport initializes");
    Rl78ProtocolB_GetTargetOps(&g16_driver, &g16_target);
    Check(g16_target.start(g16_target.context, &g16_image, 0,
                           g16_image.target_profile_uuid,
                           g16_profile.uuid.data(), 0) == PLP_OK,
          "Protocol B accepts G16 32 KiB profile");
    status = RunProtocolToTerminal(&g16_target, &g16_transport);
    Check(status.terminal_result == PLP_TERMINAL_PASS &&
              g16_transport.programmed == 32u * 1024u,
          "Protocol B completes G16 32 KiB programming flow");

    ProtocolTransport g15_8_transport;
    g15_8_transport.code_size = 8u * 1024u;
    g15_8_transport.size_code = 0x1F;
    Rl78ProtocolB g15_8_driver{};
    ProgrammerTargetOps g15_8_target{};
    auto g15_8_ops = MakeProtocolTransport(&g15_8_transport);
    const auto& g15_8_profile = rl78_programmer::Rl78G15_8KTargetProfile();
    auto g15_8_image = MakeProtocolImage(&image_context, g15_8_profile);
    Check(Rl78ProtocolB_Init(&g15_8_driver, &g15_8_ops) == PLP_OK,
          "Protocol B G15 8 KiB transport initializes");
    Rl78ProtocolB_GetTargetOps(&g15_8_driver, &g15_8_target);
    Check(g15_8_target.start(g15_8_target.context, &g15_8_image, 0,
                             g15_8_image.target_profile_uuid,
                             g15_8_profile.uuid.data(), 0) == PLP_OK &&
              RunProtocolToTerminal(&g15_8_target, &g15_8_transport).terminal_result ==
                  PLP_TERMINAL_PASS &&
              g15_8_transport.programmed == 8u * 1024u,
          "Protocol B completes G15 8 KiB programming flow");

    ProtocolTransport g16_16_transport;
    g16_16_transport.code_size = 16u * 1024u;
    g16_16_transport.size_code = 0x3F;
    Rl78ProtocolB g16_16_driver{};
    ProgrammerTargetOps g16_16_target{};
    auto g16_16_ops = MakeProtocolTransport(&g16_16_transport);
    const auto& g16_16_profile = rl78_programmer::Rl78G16_16KTargetProfile();
    auto g16_16_image = MakeProtocolImage(&image_context, g16_16_profile);
    Check(Rl78ProtocolB_Init(&g16_16_driver, &g16_16_ops) == PLP_OK,
          "Protocol B G16 16 KiB transport initializes");
    Rl78ProtocolB_GetTargetOps(&g16_16_driver, &g16_16_target);
    Check(g16_16_target.start(g16_16_target.context, &g16_16_image, 0,
                              g16_16_image.target_profile_uuid,
                              g16_16_profile.uuid.data(), 0) == PLP_OK &&
              RunProtocolToTerminal(&g16_16_target, &g16_16_transport).terminal_result ==
                  PLP_TERMINAL_PASS &&
              g16_16_transport.programmed == 16u * 1024u,
          "Protocol B completes G16 16 KiB programming flow");

    ProtocolTransport retry_transport;
    retry_transport.respond_after_enter = 2;
    Rl78ProtocolB retry_driver{};
    ProgrammerTargetOps retry_target{};
    auto retry_ops = MakeProtocolTransport(&retry_transport);
    Check(Rl78ProtocolB_Init(&retry_driver, &retry_ops) == PLP_OK,
          "Protocol B retry transport initializes");
    Rl78ProtocolB_GetTargetOps(&retry_driver, &retry_target);
    Check(retry_target.start(retry_target.context, &image, 1,
                             image.target_profile_uuid, profile.uuid.data(), 0) == PLP_OK,
          "Protocol B starts offline job");
    status = RunProtocolToTerminal(&retry_target, &retry_transport);
    Check(status.terminal_result == PLP_TERMINAL_PASS && status.attempt == 2 &&
              retry_transport.enter_calls == 2 && retry_transport.leave_calls == 1 &&
              retry_transport.prepare_calls == 2 && retry_transport.now_ms >= 600,
          "Protocol B cleans the link before each 500 ms offline retry");

    ProtocolTransport online_retry_transport;
    online_retry_transport.respond_after_enter = 2;
    Rl78ProtocolB online_retry_driver{};
    ProgrammerTargetOps online_retry_target{};
    auto online_retry_ops = MakeProtocolTransport(&online_retry_transport);
    Check(Rl78ProtocolB_Init(&online_retry_driver, &online_retry_ops) == PLP_OK,
          "Protocol B online-retry transport initializes");
    Rl78ProtocolB_GetTargetOps(&online_retry_driver, &online_retry_target);
    Check(online_retry_target.start(online_retry_target.context, &image, 0,
                                    image.target_profile_uuid,
                                    profile.uuid.data(), 0) == PLP_OK,
          "Protocol B starts online handshake-retry job");
    status = RunProtocolToTerminal(&online_retry_target,
                                   &online_retry_transport);
    Check(status.terminal_result == PLP_TERMINAL_PASS &&
              status.attempt == 2 &&
              online_retry_transport.enter_calls == 2 &&
              online_retry_transport.leave_calls == 1 &&
              online_retry_transport.programmed == 4u * 1024u,
          "Protocol B retries an online handshake before erase");

    ProtocolTransport button_retry_transport;
    button_retry_transport.respond_after_enter = 2;
    Rl78ProtocolB button_retry_driver{};
    ProgrammerTargetOps button_retry_target{};
    auto button_retry_ops = MakeProtocolTransport(&button_retry_transport);
    Check(Rl78ProtocolB_Init(&button_retry_driver, &button_retry_ops) == PLP_OK,
          "Protocol B button-retry transport initializes");
    Rl78ProtocolB_GetTargetOps(&button_retry_driver, &button_retry_target);
    Check(button_retry_target.start(button_retry_target.context, &image, 1,
                                    image.target_profile_uuid,
                                    profile.uuid.data(), 0) == PLP_OK,
          "Protocol B starts button-retry offline job");
    for (int poll = 0; poll < 200 && button_retry_transport.leave_calls == 0; ++poll) {
        ++button_retry_transport.now_ms;
        button_retry_target.poll(button_retry_target.context, &status);
    }
    const auto button_press_ms = button_retry_transport.now_ms;
    Check(button_retry_target.retry_connect != nullptr &&
              button_retry_target.retry_connect(button_retry_target.context) == PLP_OK,
          "Protocol B accepts immediate handshake retry before erase");
    button_retry_target.poll(button_retry_target.context, &status);
    Check(button_retry_transport.enter_calls == 2 &&
              button_retry_transport.now_ms == button_press_ms,
          "Protocol B button retry bypasses the 500 ms wait");
    status = RunProtocolToTerminal(&button_retry_target, &button_retry_transport);
    Check(status.terminal_result == PLP_TERMINAL_PASS && status.attempt == 2,
          "Protocol B immediate retry completes the offline job");

    ProtocolTransport snapshot_transport;
    snapshot_transport.fail_after_programmed = 17;
    snapshot_transport.failure_snapshot.valid = 1;
    snapshot_transport.failure_snapshot.tx_value = 0x11;
    snapshot_transport.failure_snapshot.tx_bit_index = 4;
    snapshot_transport.failure_snapshot.timer_state = 1;
    snapshot_transport.failure_snapshot.pin_flags =
        PLP_OFFLINE_FAILURE_PIN_PA10_INPUT;
    snapshot_transport.failure_snapshot.timer_count = 104;
    Rl78ProtocolB snapshot_driver{};
    ProgrammerTargetOps snapshot_target{};
    auto snapshot_ops = MakeProtocolTransport(&snapshot_transport);
    Check(Rl78ProtocolB_Init(&snapshot_driver, &snapshot_ops) == PLP_OK,
          "Protocol B diagnostic transport initializes");
    Rl78ProtocolB_GetTargetOps(&snapshot_driver, &snapshot_target);
    Check(snapshot_target.start(snapshot_target.context, &image, 1,
                                image.target_profile_uuid,
                                profile.uuid.data(), 0) == PLP_OK,
          "Protocol B starts diagnostic failure scenario");
    status = RunProtocolToTerminal(&snapshot_target, &snapshot_transport);
    ProgrammerTargetFailureSnapshot snapshot{};
    Check(status.terminal_result == PLP_TERMINAL_FAIL &&
              status.error == PLP_ERR_TARGET_DIRECTION &&
              status.stage == 8 && status.detail == 4 &&
              snapshot_target.get_failure_snapshot != nullptr &&
              snapshot_target.get_failure_snapshot(snapshot_target.context,
                                                     &snapshot) == PLP_OK &&
              snapshot.valid != 0u && snapshot.protocol_step == 14u &&
              snapshot.unit_byte_index == 1u && snapshot.address == 16u &&
              snapshot.tx_value == 0x11u && snapshot.tx_bit_index == 4u &&
              (snapshot.pin_flags & PLP_OFFLINE_FAILURE_LINK_VALID) != 0u &&
              (snapshot.pin_flags & PLP_OFFLINE_FAILURE_DESTRUCTIVE) != 0u,
          "Protocol B snapshots a destructive TOOL0 direction failure before cleanup");

    ProtocolTransport mismatch_transport;
    mismatch_transport.size_code = 0x1F;
    Rl78ProtocolB mismatch_driver{};
    ProgrammerTargetOps mismatch_target{};
    auto mismatch_ops = MakeProtocolTransport(&mismatch_transport);
    Check(Rl78ProtocolB_Init(&mismatch_driver, &mismatch_ops) == PLP_OK,
          "Protocol B mismatch transport initializes");
    Rl78ProtocolB_GetTargetOps(&mismatch_driver, &mismatch_target);
    Check(mismatch_target.start(mismatch_target.context, &image, 0,
                                image.target_profile_uuid, profile.uuid.data(), 0) == PLP_OK,
          "Protocol B starts exact-profile probe");
    status = RunProtocolToTerminal(&mismatch_target, &mismatch_transport);
    Check(status.terminal_result == PLP_TERMINAL_FAIL &&
              status.error == PLP_ERR_DRIVER_TARGET_MISMATCH &&
              mismatch_transport.programmed == 0,
          "Protocol B rejects target flash-size mismatch before erase permission");

    ProtocolTransport erase_error_transport;
    erase_error_transport.erase_status = 0x1A;
    Rl78ProtocolB erase_error_driver{};
    ProgrammerTargetOps erase_error_target{};
    auto erase_error_ops = MakeProtocolTransport(&erase_error_transport);
    Check(Rl78ProtocolB_Init(&erase_error_driver, &erase_error_ops) == PLP_OK,
          "Protocol B erase-error transport initializes");
    Rl78ProtocolB_GetTargetOps(&erase_error_driver, &erase_error_target);
    Check(erase_error_target.start(erase_error_target.context, &image, 0,
                                   image.target_profile_uuid,
                                   profile.uuid.data(), 0) == PLP_OK,
          "Protocol B starts erase-error scenario");
    status = RunProtocolToTerminal(&erase_error_target, &erase_error_transport);
    Check(status.terminal_result == PLP_TERMINAL_FAIL &&
              status.error == PLP_ERR_DRIVER_TARGET_STATUS &&
              status.detail == 0x1A && erase_error_transport.programmed == 0 &&
              erase_error_transport.restart_calls == 1,
          "Protocol B maps completed erase error and safely restarts target");

    ProtocolTransport write_error_transport;
    write_error_transport.unit_status = 0x1C;
    Rl78ProtocolB write_error_driver{};
    ProgrammerTargetOps write_error_target{};
    auto write_error_ops = MakeProtocolTransport(&write_error_transport);
    Check(Rl78ProtocolB_Init(&write_error_driver, &write_error_ops) == PLP_OK,
          "Protocol B write-error transport initializes");
    Rl78ProtocolB_GetTargetOps(&write_error_driver, &write_error_target);
    Check(write_error_target.start(write_error_target.context, &image, 0,
                                   image.target_profile_uuid,
                                   profile.uuid.data(), 0) == PLP_OK,
          "Protocol B starts write-error scenario");
    status = RunProtocolToTerminal(&write_error_target, &write_error_transport);
    Check(status.terminal_result == PLP_TERMINAL_FAIL &&
              status.error == PLP_ERR_DRIVER_TARGET_STATUS &&
              status.detail == 0x1C && write_error_transport.programmed == 4 &&
              write_error_transport.restart_calls == 1,
          "Protocol B maps four-byte write error at a safe response boundary");

    ProtocolTransport verify_error_transport;
    verify_error_transport.verify_status = 0x1B;
    Rl78ProtocolB verify_error_driver{};
    ProgrammerTargetOps verify_error_target{};
    auto verify_error_ops = MakeProtocolTransport(&verify_error_transport);
    Check(Rl78ProtocolB_Init(&verify_error_driver, &verify_error_ops) == PLP_OK,
          "Protocol B verify-error transport initializes");
    Rl78ProtocolB_GetTargetOps(&verify_error_driver, &verify_error_target);
    Check(verify_error_target.start(verify_error_target.context, &image, 0,
                                    image.target_profile_uuid,
                                    profile.uuid.data(), 0) == PLP_OK,
          "Protocol B starts verify-error scenario");
    status = RunProtocolToTerminal(&verify_error_target, &verify_error_transport);
    Check(status.terminal_result == PLP_TERMINAL_FAIL &&
              status.error == PLP_ERR_VERIFY_MISMATCH && status.detail == 0x1B &&
              verify_error_transport.programmed == 4u * 1024u &&
              verify_error_transport.restart_calls == 1,
          "Protocol B maps target internal verify failure");

    ProtocolTransport timeout_transport;
    timeout_transport.suppress_erase_status = true;
    Rl78ProtocolB timeout_driver{};
    ProgrammerTargetOps timeout_target{};
    auto timeout_ops = MakeProtocolTransport(&timeout_transport);
    Check(Rl78ProtocolB_Init(&timeout_driver, &timeout_ops) == PLP_OK,
          "Protocol B destructive-timeout transport initializes");
    Rl78ProtocolB_GetTargetOps(&timeout_driver, &timeout_target);
    Check(timeout_target.start(timeout_target.context, &image, 0,
                               image.target_profile_uuid,
                               profile.uuid.data(), 0) == PLP_OK,
          "Protocol B starts destructive-timeout scenario");
    status = RunProtocolToTerminal(&timeout_target, &timeout_transport);
    Check(status.terminal_result == PLP_TERMINAL_OUTCOME_UNKNOWN &&
              status.error == PLP_ERR_TARGET_TIMEOUT &&
              timeout_transport.restart_calls == 0,
          "Protocol B preserves unknown outcome after erase timeout");

    ProtocolTransport cancel_transport;
    Rl78ProtocolB cancel_driver{};
    ProgrammerTargetOps cancel_target{};
    auto cancel_ops = MakeProtocolTransport(&cancel_transport);
    Check(Rl78ProtocolB_Init(&cancel_driver, &cancel_ops) == PLP_OK,
          "Protocol B cancellation transport initializes");
    Rl78ProtocolB_GetTargetOps(&cancel_driver, &cancel_target);
    Check(cancel_target.start(cancel_target.context, &image, 0,
                              image.target_profile_uuid,
                              profile.uuid.data(), 0) == PLP_OK,
          "Protocol B starts cancellation scenario");
    ProgrammerTargetStatus cancel_status{};
    for (int poll = 0; poll < 100 && cancel_driver.destructive_started == 0; ++poll) {
        ++cancel_transport.now_ms;
        static_cast<void>(cancel_target.poll(cancel_target.context, &cancel_status));
    }
    std::uint8_t disposition = 0;
    Check(cancel_driver.destructive_started != 0 &&
              cancel_target.cancel(cancel_target.context, &disposition) == PLP_OK &&
              disposition == 1,
          "Protocol B defers cancellation after erase permission");
    status = RunProtocolToTerminal(&cancel_target, &cancel_transport);
    Check(status.terminal_result == PLP_TERMINAL_CANCELLED &&
              cancel_transport.programmed == 4u * 1024u &&
              cancel_transport.restart_calls == 1,
          "Protocol B finishes indivisible operation before terminal cancellation");

    image_context.has_data_flash = true;
    image = MakeProtocolImage(&image_context, profile);
    Check(target.start(target.context, &image, 0, image.target_profile_uuid,
                       profile.uuid.data(), 0) == PLP_ERR_IMAGE_INCOMPATIBLE,
          "Protocol B rejects unsupported G15/G16 data-flash programming");
}

struct FakeBoardIo {
    std::string calls;
    std::vector<std::uint32_t> delays;
    std::vector<std::uint8_t> led_outputs;
    std::uint16_t vdd_mv = 3300;
};

std::uint32_t FakeBoardInitialize(void* context) {
    static_cast<FakeBoardIo*>(context)->calls += 'I';
    return PLP_OK;
}

std::uint32_t FakeBoardTool0(void* context, std::uint8_t drive_low) {
    static_cast<FakeBoardIo*>(context)->calls += drive_low != 0 ? 'L' : 'T';
    return PLP_OK;
}

std::uint32_t FakeBoardUart(void* context, std::uint8_t dedicated) {
    static_cast<FakeBoardIo*>(context)->calls += dedicated != 0 ? 'D' : 'U';
    return PLP_OK;
}

std::uint32_t FakeBoardReset(void* context, std::uint8_t asserted) {
    static_cast<FakeBoardIo*>(context)->calls += asserted != 0 ? 'A' : 'R';
    return PLP_OK;
}

std::uint32_t FakeBoardPower(void* context, std::uint8_t enabled) {
    static_cast<FakeBoardIo*>(context)->calls += enabled != 0 ? 'E' : 'P';
    return PLP_OK;
}

std::uint32_t FakeBoardReadVdd(void* context, std::uint16_t* millivolts) {
    auto* board = static_cast<FakeBoardIo*>(context);
    board->calls += 'V';
    *millivolts = board->vdd_mv;
    return PLP_OK;
}

std::uint32_t FakeBoardLed(void* context, std::uint8_t on) {
    auto* board = static_cast<FakeBoardIo*>(context);
    board->calls += on != 0 ? 'N' : 'O';
    board->led_outputs.push_back(on);
    return PLP_OK;
}

void FakeBoardDelay(void* context, std::uint32_t microseconds) {
    auto* board = static_cast<FakeBoardIo*>(context);
    board->calls += microseconds == M032_BOARD_TOOL0_SETUP_US ? 'S' : 'H';
    board->delays.push_back(microseconds);
}

void TestM032BoardIo() {
    bool pb3_assigned = false;
    for (std::uint8_t index = 0; index < M032_BOARD_PIN_COUNT; ++index) {
        if (g_M032BoardPinAssignments[index].port == M032_BOARD_PORT_B &&
            g_M032BoardPinAssignments[index].pin == 3u) {
            pb3_assigned = true;
        }
    }
    Check(M032BoardProfile_Validate() == PLP_OK &&
              M032_BOARD_TARGET_VDD_SENSE_ENABLED == 0u &&
              g_M032BoardPinAssignments[0].port == M032_BOARD_PORT_A &&
              g_M032BoardPinAssignments[0].pin == 3 &&
              g_M032BoardPinAssignments[1].port == M032_BOARD_PORT_A &&
              g_M032BoardPinAssignments[1].pin == 2 &&
              g_M032BoardPinAssignments[2].port == M032_BOARD_PORT_A &&
              g_M032BoardPinAssignments[2].pin == 5 &&
              g_M032BoardPinAssignments[3].port == M032_BOARD_PORT_A &&
              g_M032BoardPinAssignments[3].pin == 4 &&
              g_M032BoardPinAssignments[4].port == M032_BOARD_PORT_B &&
              g_M032BoardPinAssignments[4].pin == 14 &&
              g_M032BoardPinAssignments[0].arduino[0] == 'D' &&
              g_M032BoardPinAssignments[1].arduino[0] == 'D' &&
              g_M032BoardPinAssignments[2].arduino[0] == 'D' &&
              g_M032BoardPinAssignments[3].arduino[0] == 'D' &&
              g_M032BoardPinAssignments[4].arduino[0] == '-' &&
              !pb3_assigned &&
              M032BoardProfile_IsReserved(M032_BOARD_PORT_B, 12) == 1 &&
              M032BoardProfile_IsReserved(M032_BOARD_PORT_B, 13) == 1 &&
              M032BoardProfile_IsReserved(M032_BOARD_PORT_B, 14) == 0 &&
              M032BoardProfile_IsStatusLed(M032_BOARD_PORT_B, 14) == 1,
          "M032 board profile keeps PB3 NC with VDD sense disabled");

    FakeBoardIo fake;
    M032BoardIoOps ops{};
    M032BoardIo board{};
    ops.context = &fake;
    ops.initialize = FakeBoardInitialize;
    ops.set_tool0_low = FakeBoardTool0;
    ops.set_uart_mode = FakeBoardUart;
    ops.set_reset = FakeBoardReset;
    ops.set_target_power = FakeBoardPower;
    ops.set_status_led = FakeBoardLed;
    ops.delay_us = FakeBoardDelay;
    ops.target_vdd_sense_enabled = M032_BOARD_TARGET_VDD_SENSE_ENABLED;
    Check(M032BoardIo_Init(&board, &ops) == PLP_OK && fake.calls == "ITRPO",
          "M032 board safe initialization order");

    fake.calls.clear();
    Check(M032BoardIo_EnterProgrammingMode(&board) == PLP_OK &&
              fake.calls == "ALSRHU" && fake.delays.size() == 2 &&
              fake.delays[0] == M032_BOARD_TOOL0_SETUP_US &&
              fake.delays[1] == M032_BOARD_TOOL0_HOLD_US &&
              board.programming_mode == 1,
          "M032 mode entry skips the disabled VDD interlock");

    Check(M032BoardIo_LeaveSafe(&board) == PLP_OK && board.programming_mode == 0,
          "M032 board safe release");
    std::uint16_t unused_vdd = 0;
    Check(M032BoardIo_ReadTargetVdd(&board, &unused_vdd) ==
              PLP_ERR_CAPABILITY_UNSUPPORTED,
          "M032 disabled VDD sense does not fabricate a voltage reading");

    FakeBoardIo sensed_fake;
    M032BoardIoOps sensed_ops = ops;
    M032BoardIo sensed_board{};
    sensed_ops.context = &sensed_fake;
    sensed_ops.read_target_vdd_mv = FakeBoardReadVdd;
    sensed_ops.target_vdd_sense_enabled = 1u;
    Check(M032BoardIo_Init(&sensed_board, &sensed_ops) == PLP_OK,
          "Board I/O retains an optional sensed-profile capability");
    sensed_fake.calls.clear();
    Check(M032BoardIo_EnterProgrammingMode(&sensed_board) == PLP_OK &&
              sensed_fake.calls == "VALSRHU" &&
              sensed_board.programming_mode == 1,
          "Optional sensed profile validates VDD before mode entry");
    Check(M032BoardIo_LeaveSafe(&sensed_board) == PLP_OK,
          "Optional sensed profile leaves programming mode safely");
    sensed_fake.calls.clear();
    sensed_fake.vdd_mv = 2000;
    Check(M032BoardIo_EnterProgrammingMode(&sensed_board) ==
              PLP_ERR_DRIVER_TARGET_STATUS &&
              sensed_fake.calls == "V" &&
              sensed_board.programming_mode == 0,
          "Optional sensed profile rejects unsafe target VDD");
}

void TestNulink2MeBoardProfile() {
    bool pb12_assigned = false;
    for (std::uint8_t index = 0; index < NULINK2ME_PIN_COUNT; ++index) {
        if (g_Nulink2MePinAssignments[index].port == NULINK2ME_PORT_B &&
            g_Nulink2MePinAssignments[index].pin == 12u) {
            pb12_assigned = true;
        }
    }
    Check(Nulink2MeBoardProfile_Validate() == PLP_OK &&
              NULINK2ME_TARGET_VDD_SENSE_ENABLED == 0u &&
              g_Nulink2MePinAssignments[NULINK2ME_ROLE_TOOL0_TX].port ==
                  NULINK2ME_PORT_A &&
              g_Nulink2MePinAssignments[NULINK2ME_ROLE_TOOL0_TX].pin == 9 &&
              g_Nulink2MePinAssignments[NULINK2ME_ROLE_TOOL0_RX].pin == 10 &&
              g_Nulink2MePinAssignments[NULINK2ME_ROLE_RESET].port ==
                  NULINK2ME_PORT_F &&
              g_Nulink2MePinAssignments[NULINK2ME_ROLE_RESET].pin == 5 &&
              g_Nulink2MePinAssignments[NULINK2ME_ROLE_BUTTON].pin == 7 &&
              g_Nulink2MePinAssignments[NULINK2ME_ROLE_SPIM_MOSI].pin == 0 &&
              g_Nulink2MePinAssignments[NULINK2ME_ROLE_SPIM_MISO].pin == 1 &&
              g_Nulink2MePinAssignments[NULINK2ME_ROLE_SPIM_CLK].pin == 2 &&
              g_Nulink2MePinAssignments[NULINK2ME_ROLE_SPIM_SS].pin == 3 &&
              !pb12_assigned,
          "Nu-Link2-Me M484 pins match TOOL0, reset, KEY1 and SPIM with PB12 NC");
}

void TestNulink2MeOfflineRecovery() {
    Nulink2MeOfflineRecovery recovery{};
    ProgrammerCore core{};
    core.active_source_kind = 1u;
    core.operation_state = PLP_OPERATION_RUNNING;
    core.job_stage = 4u;
    Nulink2MeOfflineRecovery_Init(&recovery);

    Check(Nulink2MeOfflineRecovery_Process(&recovery, &core, 100u) ==
              NULINK2ME_OFFLINE_RECOVERY_NONE &&
              Nulink2MeOfflineRecovery_Process(&recovery, &core, 1099u) ==
              NULINK2ME_OFFLINE_RECOVERY_NONE &&
              Nulink2MeOfflineRecovery_Process(&recovery, &core, 1100u) ==
              NULINK2ME_OFFLINE_RECOVERY_CONNECT_RESTART &&
              Nulink2MeOfflineRecovery_Process(&recovery, &core, 2100u) ==
              NULINK2ME_OFFLINE_RECOVERY_NONE,
          "Nu-Link2-Me stalled green connect indication emits one pending reset");
    Nulink2MeOfflineRecovery_Complete(
        &recovery, NULINK2ME_OFFLINE_RECOVERY_CONNECT_RESTART, 1u, 1100u);
    Check(Nulink2MeOfflineRecovery_Process(&recovery, &core, 2099u) ==
              NULINK2ME_OFFLINE_RECOVERY_NONE &&
              Nulink2MeOfflineRecovery_Process(&recovery, &core, 2100u) ==
              NULINK2ME_OFFLINE_RECOVERY_CONNECT_RESTART,
          "Nu-Link2-Me connect recovery remains bounded to one reset per second");
    Nulink2MeOfflineRecovery_Complete(
        &recovery, NULINK2ME_OFFLINE_RECOVERY_CONNECT_RESTART, 1u, 2100u);

    core.job_stage = 8u;
    Check(Nulink2MeOfflineRecovery_Process(&recovery, &core, 4000u) ==
              NULINK2ME_OFFLINE_RECOVERY_NONE,
          "Nu-Link2-Me normal green programming activity is never reset");
    core.operation_state = PLP_OPERATION_FAILED;
    core.terminal_result = PLP_TERMINAL_FAIL;
    core.job_error = PLP_ERR_TARGET_DIRECTION;
    Check(Nulink2MeOfflineRecovery_Process(&recovery, &core, 4100u) ==
              NULINK2ME_OFFLINE_RECOVERY_PARK_TARGET &&
              Nulink2MeOfflineRecovery_Process(&recovery, &core, 4101u) ==
              NULINK2ME_OFFLINE_RECOVERY_NONE &&
              recovery.terminal_parked == 0u,
          "Nu-Link2-Me collision failure immediately requests a parked target");
    Nulink2MeOfflineRecovery_Complete(
        &recovery, NULINK2ME_OFFLINE_RECOVERY_PARK_TARGET, 1u, 4100u);
    Check(recovery.terminal_parked == 1u &&
              Nulink2MeOfflineRecovery_Process(&recovery, &core, 5099u) ==
              NULINK2ME_OFFLINE_RECOVERY_NONE &&
              Nulink2MeOfflineRecovery_Process(&recovery, &core, 5100u) ==
              NULINK2ME_OFFLINE_RECOVERY_COLLISION_RETRY,
          "Nu-Link2-Me collision holds reset for one second before full retry");
    Nulink2MeOfflineRecovery_Complete(
        &recovery, NULINK2ME_OFFLINE_RECOVERY_COLLISION_RETRY, 1u, 5100u);
    Check(recovery.automatic_retry_count == 1u &&
              recovery.condition == 0u,
          "Nu-Link2-Me first automatic collision retry preserves its budget");

    core.operation_state = PLP_OPERATION_RUNNING;
    core.job_stage = 8u;
    Check(Nulink2MeOfflineRecovery_Process(&recovery, &core, 5200u) ==
              NULINK2ME_OFFLINE_RECOVERY_NONE,
          "Nu-Link2-Me automatic retry is a new normal programming job");
    core.operation_state = PLP_OPERATION_FAILED;
    core.terminal_result = PLP_TERMINAL_FAIL;
    core.job_error = PLP_ERR_TARGET_DIRECTION;
    Check(Nulink2MeOfflineRecovery_Process(&recovery, &core, 5300u) ==
              NULINK2ME_OFFLINE_RECOVERY_PARK_TARGET,
          "Nu-Link2-Me second collision immediately parks the target again");
    Nulink2MeOfflineRecovery_Complete(
        &recovery, NULINK2ME_OFFLINE_RECOVERY_PARK_TARGET, 1u, 5300u);
    Check(Nulink2MeOfflineRecovery_Process(&recovery, &core, 6300u) ==
              NULINK2ME_OFFLINE_RECOVERY_COLLISION_RETRY,
          "Nu-Link2-Me second bounded full retry starts after one second");
    Nulink2MeOfflineRecovery_Complete(
        &recovery, NULINK2ME_OFFLINE_RECOVERY_COLLISION_RETRY, 1u, 6300u);
    Check(recovery.automatic_retry_count ==
              NULINK2ME_OFFLINE_COLLISION_MAX_RETRIES,
          "Nu-Link2-Me collision automatic retry budget is two attempts");

    core.operation_state = PLP_OPERATION_RUNNING;
    core.job_stage = 8u;
    Nulink2MeOfflineRecovery_Process(&recovery, &core, 6400u);
    core.operation_state = PLP_OPERATION_FAILED;
    core.terminal_result = PLP_TERMINAL_FAIL;
    core.job_error = PLP_ERR_TARGET_DIRECTION;
    Check(Nulink2MeOfflineRecovery_Process(&recovery, &core, 6500u) ==
              NULINK2ME_OFFLINE_RECOVERY_PARK_TARGET,
          "Nu-Link2-Me final collision still parks the partially written target");
    Nulink2MeOfflineRecovery_Complete(
        &recovery, NULINK2ME_OFFLINE_RECOVERY_PARK_TARGET, 1u, 6500u);
    Check(Nulink2MeOfflineRecovery_Process(&recovery, &core, 7500u) ==
              NULINK2ME_OFFLINE_RECOVERY_NONE &&
              recovery.terminal_latched == 1u &&
              recovery.automatic_retry_count == 2u,
          "Nu-Link2-Me exhausts at three total attempts and waits for KEY1");

    Nulink2MeOfflineRecovery_NotifyJobStarted(&recovery);
    core.operation_state = PLP_OPERATION_FAILED;
    core.terminal_result = PLP_TERMINAL_FAIL;
    core.job_error = PLP_ERR_TARGET_TIMEOUT;
    Check(recovery.automatic_retry_count == 0u &&
              Nulink2MeOfflineRecovery_Process(&recovery, &core, 9000u) ==
              NULINK2ME_OFFLINE_RECOVERY_PARK_TARGET,
          "Nu-Link2-Me physical KEY1 resets the budget and parks other failures");
    Nulink2MeOfflineRecovery_Complete(
        &recovery, NULINK2ME_OFFLINE_RECOVERY_PARK_TARGET, 1u, 9000u);
    Check(Nulink2MeOfflineRecovery_Process(&recovery, &core, 10000u) ==
              NULINK2ME_OFFLINE_RECOVERY_NONE &&
              recovery.terminal_latched == 1u,
          "Nu-Link2-Me non-collision failure never starts destructive retry");

    Nulink2MeOfflineRecovery_NotifyJobStarted(&recovery);
    core.operation_state = PLP_OPERATION_FAILED;
    core.terminal_result = PLP_TERMINAL_OUTCOME_UNKNOWN;
    core.job_error = PLP_ERR_OUTCOME_UNKNOWN;
    Check(Nulink2MeOfflineRecovery_Process(&recovery, &core, 11000u) ==
              NULINK2ME_OFFLINE_RECOVERY_PARK_TARGET,
          "Nu-Link2-Me unknown outcome immediately parks the target");
    Nulink2MeOfflineRecovery_Complete(
        &recovery, NULINK2ME_OFFLINE_RECOVERY_PARK_TARGET, 1u, 11000u);
    Check(Nulink2MeOfflineRecovery_Process(&recovery, &core, 12000u) ==
              NULINK2ME_OFFLINE_RECOVERY_NONE &&
              recovery.terminal_latched == 1u,
          "Nu-Link2-Me unknown outcome never starts destructive retry");

    Nulink2MeOfflineRecovery_NotifyJobStarted(&recovery);
    core.operation_state = PLP_OPERATION_CANCELLED;
    core.terminal_result = PLP_TERMINAL_CANCELLED;
    core.job_error = PLP_OK;
    Check(Nulink2MeOfflineRecovery_Process(&recovery, &core, 13000u) ==
              NULINK2ME_OFFLINE_RECOVERY_PARK_TARGET,
          "Nu-Link2-Me cancelled offline job immediately parks the target");
    Nulink2MeOfflineRecovery_Complete(
        &recovery, NULINK2ME_OFFLINE_RECOVERY_PARK_TARGET, 1u, 13000u);
    Check(Nulink2MeOfflineRecovery_Process(&recovery, &core, 14000u) ==
              NULINK2ME_OFFLINE_RECOVERY_NONE &&
              recovery.terminal_latched == 1u,
          "Nu-Link2-Me cancelled job waits for the next physical KEY1");

    Nulink2MeOfflineRecovery_Init(&recovery);
    core.active_source_kind = 0u;
    core.operation_state = PLP_OPERATION_FAILED;
    Check(Nulink2MeOfflineRecovery_Process(&recovery, &core, 20000u) ==
              NULINK2ME_OFFLINE_RECOVERY_NONE,
          "Nu-Link2-Me recovery never acts on an online job");
}

void TestM032StatusLed() {
    FakeBoardIo fake;
    M032BoardIoOps ops{};
    M032BoardIo board{};
    M032Programmer programmer{};
    ops.context = &fake;
    ops.initialize = FakeBoardInitialize;
    ops.set_tool0_low = FakeBoardTool0;
    ops.set_uart_mode = FakeBoardUart;
    ops.set_reset = FakeBoardReset;
    ops.set_target_power = FakeBoardPower;
    ops.set_status_led = FakeBoardLed;
    ops.delay_us = FakeBoardDelay;
    ops.target_vdd_sense_enabled = M032_BOARD_TARGET_VDD_SENSE_ENABLED;
    Check(M032BoardIo_Init(&board, &ops) == PLP_OK,
          "M032 status LED board setup");
    M032Programmer_Init(&programmer);
    Check(M032Programmer_AttachBoardIo(&programmer, &board) == PLP_OK,
          "M032 status LED attach");
    fake.led_outputs.clear();

    M032Programmer_UpdateIndicator(&programmer, 0);
    M032Programmer_UpdateIndicator(&programmer, 150);
    programmer.core.state = PLP_STATE_PROGRAMMING;
    M032Programmer_UpdateIndicator(&programmer, 0);
    M032Programmer_UpdateIndicator(&programmer, 125);
    programmer.core.state = PLP_STATE_SESSION;
    programmer.core.operation_state = PLP_OPERATION_COMPLETED;
    programmer.core.terminal_result = PLP_TERMINAL_PASS;
    M032Programmer_UpdateIndicator(&programmer, 0);
    programmer.core.operation_state = PLP_OPERATION_FAILED;
    programmer.core.terminal_result = PLP_TERMINAL_FAIL;
    M032Programmer_UpdateIndicator(&programmer, 150);
    M032Programmer_UpdateIndicator(&programmer, 250);

    const std::vector<std::uint8_t> expected{1, 0, 1, 0, 1, 0, 1};
    Check(fake.led_outputs == expected,
          "M032 single LED ready/program/pass/fail patterns");
}

void TestM032TestBoardProfile() {
    Check(M032TestBoardProfile_Validate() == PLP_OK,
          "M032 TEST BOARD pin and APROM profile validates");
    Check(M032_TEST_TOOL0_TX_PORT == M032_TEST_BOARD_PORT_B &&
              M032_TEST_TOOL0_TX_PIN == 9u &&
              M032_TEST_TOOL0_RX_PORT == M032_TEST_BOARD_PORT_B &&
              M032_TEST_TOOL0_RX_PIN == 10u &&
              M032_TEST_RESET_PIN == 8u,
          "M032 TEST BOARD uses PB9 TX/PB10 software RX and PB8 reset");
    Check(M032_TEST_PC_UART_RX_PORT == M032_TEST_BOARD_PORT_F &&
              M032_TEST_PC_UART_RX_PIN == 2u &&
              M032_TEST_PC_UART_TX_PIN == 3u &&
              M032_TEST_BUTTON_PIN == 2u,
          "M032 TEST BOARD uses PF2/PF3 VCOM and PC2 button");
    Check(M032_TEST_APP_BASE == 0x4000u &&
              M032_TEST_APP_SIZE == 0xBC00u &&
              M032_TEST_METADATA_BASE == 0xFC00u &&
              M032_TEST_SLOT_BASE == 0x10000u &&
              M032_TEST_SLOT_SIZE == 0x10000u,
          "M032 TEST BOARD APROM partition reserves a 64 KiB offline slot");
}

} // namespace

int main() {
    try {
        TestIntegrity();
        TestSessionFileLogger();
        TestValidatedImageSource();
        TestBridgeFirmwareUpdateContracts();
        TestPlp();
        TestSerialPlpFramer();
        TestSRecord();
        TestIntelHex();
        TestOip();
        TestFirmwareCore();
        TestFirmwareBootloaderEntry();
        TestFirmwareProgrammerReset();
        TestM032Adapter();
        TestM032PackageStore();
        TestNulink2MeSpimGeometry();
        TestNulink2MeImageCache();
        TestNulink2MeOnlineOfflineSoftwareFlow();
        TestRl78ProtocolB();
        TestM032BoardIo();
        TestNulink2MeBoardProfile();
        TestNulink2MeOfflineRecovery();
        TestM032StatusLed();
        TestM032TestBoardProfile();
    } catch (const std::exception& ex) {
        std::cerr << "UNCAUGHT: " << ex.what() << '\n';
        return 2;
    }
    if (g_failures != 0) {
        std::cerr << g_failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All native conformance tests passed\n";
    return 0;
}

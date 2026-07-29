#include "rl78_programmer/offline_image.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "rl78_programmer/integrity.h"
#include "rl78_programmer/oip_format.h"
#include "rl78_programmer/plp_contract.h"
#include "rl78_programmer/plp_frame.h"

namespace rl78_programmer {
namespace {

struct OipSection {
    std::uint16_t type = 0;
    std::uint16_t flags = 0;
    std::uint8_t policy_class = 0;
    std::uint32_t address = 0;
    std::vector<std::uint8_t> data;
};

void WriteLe64(std::uint8_t* out, std::uint64_t value) {
    for (std::size_t i = 0; i < 8; ++i) {
        out[i] = static_cast<std::uint8_t>(value >> (i * 8));
    }
}

std::size_t Align4(std::size_t value) {
    return (value + 3u) & ~std::size_t(3u);
}

bool IsZero(const std::array<std::uint8_t, 16>& value) {
    return std::all_of(value.begin(), value.end(), [](std::uint8_t byte) { return byte == 0; });
}

std::array<std::uint8_t, 16> DeriveUuid(const std::array<std::uint8_t, 32>& digest,
                                        std::uint8_t domain) {
    std::array<std::uint8_t, 16> uuid{};
    for (std::size_t i = 0; i < uuid.size(); ++i) {
        uuid[i] = static_cast<std::uint8_t>(digest[i] ^ digest[i + 16] ^ domain);
    }
    uuid[6] = static_cast<std::uint8_t>((uuid[6] & 0x0Fu) | 0x50u);
    uuid[8] = static_cast<std::uint8_t>((uuid[8] & 0x3Fu) | 0x80u);
    return uuid;
}

std::vector<OipSection> SplitSections(const SRecordImage& image,
                                      const TargetProfile& profile) {
    std::vector<OipSection> sections;
    for (const auto& source : image.segments) {
        for (std::size_t i = 0; i < source.data.size(); ++i) {
            const std::uint32_t address = source.address + static_cast<std::uint32_t>(i);
            const auto& region = ClassifyTargetAddress(profile, address);
            const std::uint8_t policy_class = region.policy_class;
            const std::uint16_t type = region.section_type;
            if (sections.empty() ||
                sections.back().type != type ||
                sections.back().address + sections.back().data.size() != address) {
                OipSection section;
                section.type = type;
                section.policy_class = policy_class;
                section.address = address;
                section.flags = RPP_OIP_SECTION_REQUIRED | RPP_OIP_SECTION_VERIFY_REQUIRED;
                if (region.executable) {
                    section.flags |= RPP_OIP_SECTION_EXECUTABLE;
                }
                if (region.sensitive) {
                    section.flags |= RPP_OIP_SECTION_SENSITIVE;
                }
                sections.push_back(std::move(section));
            }
            sections.back().data.push_back(source.data[i]);
        }
    }
    return sections;
}

void AppendTlv(std::vector<std::uint8_t>* manifest,
               std::uint16_t type,
               bool required,
               const std::vector<std::uint8_t>& value) {
    if (manifest == nullptr || value.size() > 0xFFFFFFFFu) {
        throw std::runtime_error("invalid OIP manifest TLV");
    }
    const std::size_t start = manifest->size();
    manifest->resize(start + 8 + value.size(), 0);
    Rpp_WriteLe16(manifest->data() + start, type);
    Rpp_WriteLe16(manifest->data() + start + 2, required ? 1u : 0u);
    Rpp_WriteLe32(manifest->data() + start + 4, static_cast<std::uint32_t>(value.size()));
    std::copy(value.begin(), value.end(), manifest->begin() + static_cast<std::ptrdiff_t>(start + 8));
    manifest->resize(Align4(manifest->size()), 0);
}

void AppendStringTlv(std::vector<std::uint8_t>* manifest,
                     std::uint16_t type,
                     bool required,
                     const std::string& text) {
    AppendTlv(manifest,
              type,
              required,
              std::vector<std::uint8_t>(text.begin(), text.end()));
}

std::array<std::uint8_t, 32> HashImage(const SRecordImage& image) {
    RppSha256Context context;
    std::array<std::uint8_t, 32> digest{};
    Rpp_Sha256Init(&context);
    for (const auto& segment : image.segments) {
        std::uint8_t address[4];
        Rpp_WriteLe32(address, segment.address);
        Rpp_Sha256Update(&context, address, sizeof(address));
        Rpp_Sha256Update(&context, segment.data.data(), segment.data.size());
    }
    Rpp_Sha256Final(&context, digest.data());
    return digest;
}

std::uint32_t CanonicalPackageCrc(std::vector<std::uint8_t> bytes) {
    std::fill(bytes.begin() + RPP_OIP_HEADER_CRC_OFFSET,
              bytes.begin() + RPP_OIP_PACKAGE_CRC_OFFSET + 4,
              std::uint8_t{0});
    return Rpp_Crc32IsoHdlc(bytes.data(), bytes.size());
}

} // namespace

OfflineImagePackage BuildOfflineImagePackage(const SRecordImage& image,
                                              OipBuildOptions options) {
    ValidateTargetProfile(options.target_profile);
    const auto sections = SplitSections(image, options.target_profile);
    if (sections.empty()) {
        throw std::runtime_error("cannot build OIP without image sections");
    }
    if (sections.size() > 1024) {
        throw std::runtime_error("too many OIP sections");
    }

    const auto image_digest = HashImage(image);
    if (IsZero(options.image_uuid)) {
        options.image_uuid = DeriveUuid(image_digest, 0x11u);
    }
    if (IsZero(options.package_uuid)) {
        options.package_uuid = DeriveUuid(image_digest, 0x22u);
    }
    if (IsZero(options.project_uuid)) {
        options.project_uuid = DeriveUuid(image_digest, 0x33u);
    }
    if (options.build_time_ms == 0) {
        options.build_time_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
    }

    std::vector<std::uint8_t> manifest;
    AppendStringTlv(&manifest, 0x0001u, true, options.project_name);
    AppendStringTlv(&manifest, 0x0002u, true, options.project_revision);
    AppendStringTlv(&manifest, 0x0003u, true, options.target_profile.display_name);
    AppendTlv(&manifest, 0x0004u, true,
              std::vector<std::uint8_t>(options.target_profile.uuid.begin(),
                                        options.target_profile.uuid.end()));
    AppendStringTlv(&manifest, 0x0005u, true, options.firmware_version);
    std::vector<std::uint8_t> provenance(options.source_name.begin(), options.source_name.end());
    provenance.insert(provenance.end(), options.source_sha256.begin(), options.source_sha256.end());
    AppendTlv(&manifest, 0x0006u, true, provenance);
    AppendStringTlv(&manifest, 0x0007u, true, "rpp-image-builder/1.0.0");
    AppendStringTlv(&manifest, 0x0008u, true, "erase-program-crc-reset");
    const bool has_sensitive = std::any_of(sections.begin(), sections.end(), [](const OipSection& section) {
        return (section.flags & RPP_OIP_SECTION_SENSITIVE) != 0;
    });
    if (has_sensitive) {
        AppendStringTlv(&manifest, 0x0009u, true, "program-deferred-explicit");
    }
    AppendStringTlv(&manifest, 0x000Eu, true, options.target_profile.driver_requirement);

    const std::size_t manifest_offset = RPP_OIP_HEADER_SIZE;
    const std::size_t descriptor_offset = Align4(manifest_offset + manifest.size());
    const std::size_t payload_offset = Align4(descriptor_offset + sections.size() * RPP_OIP_DESCRIPTOR_SIZE);
    std::size_t total_payload = 0;
    for (const auto& section : sections) {
        total_payload += section.data.size();
    }
    if (payload_offset + total_payload > 0xFFFFFFFFu) {
        throw std::runtime_error("OIP package exceeds V1 size limit");
    }

    OfflineImagePackage package;
    package.bytes.resize(payload_offset + total_payload, 0);
    package.package_uuid = options.package_uuid;
    package.target_profile_uuid = options.target_profile.uuid;
    package.section_count = sections.size();
    std::copy(manifest.begin(), manifest.end(), package.bytes.begin() + static_cast<std::ptrdiff_t>(manifest_offset));

    std::size_t current_payload = payload_offset;
    for (std::size_t i = 0; i < sections.size(); ++i) {
        const auto& section = sections[i];
        auto* descriptor = package.bytes.data() + descriptor_offset + i * RPP_OIP_DESCRIPTOR_SIZE;
        Rpp_WriteLe16(descriptor, section.type);
        Rpp_WriteLe16(descriptor + 2, section.flags);
        Rpp_WriteLe32(descriptor + 4, static_cast<std::uint32_t>(i + 1));
        Rpp_WriteLe32(descriptor + 8, section.address);
        Rpp_WriteLe32(descriptor + 12, static_cast<std::uint32_t>(section.data.size()));
        Rpp_WriteLe32(descriptor + 16, static_cast<std::uint32_t>(section.data.size()));
        Rpp_WriteLe32(descriptor + 20, static_cast<std::uint32_t>(current_payload));
        descriptor[30] = 1u;
        descriptor[31] = 0xFFu;
        Rpp_WriteLe32(descriptor + 32, Rpp_Crc32IsoHdlc(section.data.data(), section.data.size()));
        Rpp_Sha256(section.data.data(), section.data.size(), descriptor + 36);
        descriptor[70] = 1u;
        descriptor[80] = 3u;
        descriptor[81] = section.policy_class;
        std::copy(section.data.begin(), section.data.end(),
                  package.bytes.begin() + static_cast<std::ptrdiff_t>(current_payload));
        current_payload += section.data.size();
    }

    auto* header = package.bytes.data();
    header[0] = RPP_OIP_MAGIC_0;
    header[1] = RPP_OIP_MAGIC_1;
    header[2] = RPP_OIP_MAGIC_2;
    header[3] = RPP_OIP_MAGIC_3;
    Rpp_WriteLe16(header + 4, RPP_OIP_HEADER_SIZE);
    header[6] = RPP_OIP_VERSION_MAJOR;
    header[7] = RPP_OIP_VERSION_MINOR;
    std::uint32_t flags = 0;
    if (options.allow_offline) flags |= RPP_OIP_FLAG_ALLOW_OFFLINE;
    if (options.allow_online) flags |= RPP_OIP_FLAG_ALLOW_ONLINE;
    Rpp_WriteLe32(header + 8, flags);
    Rpp_WriteLe32(header + 12, static_cast<std::uint32_t>(package.bytes.size()));
    Rpp_WriteLe32(header + 16, static_cast<std::uint32_t>(manifest_offset));
    Rpp_WriteLe32(header + 20, static_cast<std::uint32_t>(manifest.size()));
    Rpp_WriteLe32(header + 24, static_cast<std::uint32_t>(descriptor_offset));
    Rpp_WriteLe16(header + 28, static_cast<std::uint16_t>(sections.size()));
    Rpp_WriteLe16(header + 30, RPP_OIP_DESCRIPTOR_SIZE);
    Rpp_WriteLe32(header + 32, static_cast<std::uint32_t>(payload_offset));
    Rpp_WriteLe32(header + 36, static_cast<std::uint32_t>(total_payload));
    std::copy(options.package_uuid.begin(), options.package_uuid.end(), header + 40);
    std::copy(options.project_uuid.begin(), options.project_uuid.end(), header + 56);
    std::copy(options.image_uuid.begin(), options.image_uuid.end(), header + 72);
    std::copy(options.target_profile.uuid.begin(), options.target_profile.uuid.end(), header + 88);
    std::copy(options.target_profile.version.begin(), options.target_profile.version.end(), header + 104);
    header[108] = 1u;
    WriteLe64(header + 112, options.build_time_ms);
    Rpp_WriteLe32(header + 120, options.release_sequence);
    Rpp_WriteLe32(header + 124,
                  static_cast<std::uint32_t>(PLP_CAP_TARGET_PROGRAM |
                                             (options.allow_offline ? PLP_CAP_OFFLINE_PACKAGE : 0u) |
                                             (options.allow_offline ? PLP_CAP_ATOMIC_ACTIVATION : 0u)));
    Rpp_Sha256(package.bytes.data() + manifest_offset, manifest.size(), header + 128);
    Rpp_Sha256(package.bytes.data() + payload_offset, total_payload, header + 160);

    std::array<std::uint8_t, RPP_OIP_HEADER_SIZE> header_copy{};
    std::copy(header, header + RPP_OIP_HEADER_SIZE, header_copy.begin());
    std::fill(header_copy.begin() + RPP_OIP_HEADER_CRC_OFFSET,
              header_copy.begin() + RPP_OIP_PACKAGE_CRC_OFFSET + 4,
              std::uint8_t{0});
    Rpp_WriteLe32(header + RPP_OIP_HEADER_CRC_OFFSET,
                  Rpp_Crc32IsoHdlc(header_copy.data(), header_copy.size()));
    package.crc32 = CanonicalPackageCrc(package.bytes);
    Rpp_WriteLe32(header + RPP_OIP_PACKAGE_CRC_OFFSET, package.crc32);
    Rpp_Sha256(package.bytes.data(), package.bytes.size(), package.sha256.data());

    RppOipInfo validated{};
    const std::uint32_t status = Rpp_OipValidateBasic(package.bytes.data(), package.bytes.size(), &validated);
    if (status != PLP_OK) {
        throw std::runtime_error("generated OIP failed self-validation, status=" + std::to_string(status));
    }
    return package;
}

void SaveOfflineImagePackage(const OfflineImagePackage& package,
                             const std::wstring& path) {
    std::ofstream stream(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("cannot create offline image package");
    }
    stream.write(reinterpret_cast<const char*>(package.bytes.data()),
                 static_cast<std::streamsize>(package.bytes.size()));
    stream.flush();
    if (!stream) {
        throw std::runtime_error("failed to save offline image package");
    }
}

} // namespace rl78_programmer

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "srecord.h"
#include "target_profile.h"

namespace rl78_programmer {

struct OipBuildOptions {
    std::string project_name = "RL78/G15 Project";
    std::string project_revision = "1";
    std::string firmware_version = "0.0.0";
    std::string source_name = "firmware.mot";
    std::array<std::uint8_t, 16> package_uuid{};
    std::array<std::uint8_t, 16> project_uuid{};
    std::array<std::uint8_t, 16> image_uuid{};
    std::array<std::uint8_t, 32> source_sha256{};
    TargetProfile target_profile = Rl78G15_8KTargetProfile();
    std::uint64_t build_time_ms = 0;
    std::uint32_t release_sequence = 1;
    bool allow_online = true;
    bool allow_offline = true;
};

struct OfflineImagePackage {
    std::vector<std::uint8_t> bytes;
    std::array<std::uint8_t, 16> package_uuid{};
    std::array<std::uint8_t, 16> target_profile_uuid{};
    std::array<std::uint8_t, 32> sha256{};
    std::uint32_t crc32 = 0;
    std::size_t section_count = 0;
};

OfflineImagePackage BuildOfflineImagePackage(const SRecordImage& image,
                                              OipBuildOptions options);
void SaveOfflineImagePackage(const OfflineImagePackage& package,
                             const std::wstring& path);

} // namespace rl78_programmer

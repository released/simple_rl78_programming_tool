#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace rl78_programmer {

struct TargetMemoryRegion {
    std::uint32_t start_address = 0;
    std::uint32_t end_address = 0;
    std::uint16_t section_type = 0;
    std::uint8_t policy_class = 0;
    bool executable = false;
    bool sensitive = false;
};

struct TargetProfile {
    std::string profile_id;
    std::string display_name;
    std::string driver_requirement;
    std::array<std::uint8_t, 16> uuid{};
    std::array<std::uint8_t, 4> version{{1, 0, 0, 0}};
    std::vector<TargetMemoryRegion> regions;
};

const TargetProfile& Rl78G15_4KTargetProfile();
const TargetProfile& Rl78G15_8KTargetProfile();
const TargetProfile& Rl78G16_16KTargetProfile();
const TargetProfile& Rl78G16_32KTargetProfile();
void ValidateTargetProfile(const TargetProfile& profile);
const TargetMemoryRegion& ClassifyTargetAddress(const TargetProfile& profile,
                                                std::uint32_t address);

} // namespace rl78_programmer

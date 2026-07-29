#include "rl78_programmer/target_profile.h"

#include <limits>
#include <stdexcept>

#include "rl78_programmer/oip_format.h"

namespace rl78_programmer {

const TargetProfile& Rl78G15_4KTargetProfile() {
    static const TargetProfile profile{
        "rl78-g15-4k",
        "RL78/G15 4 KB",
        "rl78-protocol-b/1.0.0",
        {0x52, 0x4C, 0x37, 0x38, 0x47, 0x31, 0x55, 0x00,
         0x80, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x01},
        {1, 0, 0, 0},
        {
            {0x000C0u, 0x000C3u, RPP_OIP_SECTION_OPTION, 1, false, true},
            {0x000C4u, 0x000CDu, RPP_OIP_SECTION_SECURITY, 2, false, true},
            {0x00000u, 0x00FFFu, RPP_OIP_SECTION_CODE_FLASH, 0, true, false},
            {0x09000u, 0x093FFu, RPP_OIP_SECTION_DATA_FLASH, 0, false, false}
        }
    };
    return profile;
}

const TargetProfile& Rl78G15_8KTargetProfile() {
    static const TargetProfile profile{
        "rl78-g15-8k",
        "RL78/G15 8 KB",
        "rl78-protocol-b/1.0.0",
        {0x52, 0x4C, 0x37, 0x38, 0x47, 0x31, 0x55, 0x00,
         0x80, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x01},
        {1, 0, 0, 0},
        {
            {0x000C0u, 0x000C3u, RPP_OIP_SECTION_OPTION, 1, false, true},
            {0x000C4u, 0x000CDu, RPP_OIP_SECTION_SECURITY, 2, false, true},
            {0x00000u, 0x01FFFu, RPP_OIP_SECTION_CODE_FLASH, 0, true, false},
            {0x09000u, 0x093FFu, RPP_OIP_SECTION_DATA_FLASH, 0, false, false}
        }
    };
    return profile;
}

const TargetProfile& Rl78G16_16KTargetProfile() {
    static const TargetProfile profile{
        "rl78-g16-16k",
        "RL78/G16 16 KB",
        "rl78-protocol-b/1.0.0",
        {0x52, 0x4C, 0x37, 0x38, 0x47, 0x31, 0x56, 0x00,
         0x80, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x01},
        {1, 0, 0, 0},
        {
            {0x000C0u, 0x000C3u, RPP_OIP_SECTION_OPTION, 1, false, true},
            {0x000C4u, 0x000CDu, RPP_OIP_SECTION_SECURITY, 2, false, true},
            {0x00000u, 0x03FFFu, RPP_OIP_SECTION_CODE_FLASH, 0, true, false},
            {0x09000u, 0x093FFu, RPP_OIP_SECTION_DATA_FLASH, 0, false, false}
        }
    };
    return profile;
}

const TargetProfile& Rl78G16_32KTargetProfile() {
    static const TargetProfile profile{
        "rl78-g16-32k",
        "RL78/G16 32 KB",
        "rl78-protocol-b/1.0.0",
        {0x52, 0x4C, 0x37, 0x38, 0x47, 0x31, 0x56, 0x00,
         0x80, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x01},
        {1, 0, 0, 0},
        {
            {0x000C0u, 0x000C3u, RPP_OIP_SECTION_OPTION, 1, false, true},
            {0x000C4u, 0x000CDu, RPP_OIP_SECTION_SECURITY, 2, false, true},
            {0x00000u, 0x07FFFu, RPP_OIP_SECTION_CODE_FLASH, 0, true, false},
            {0x09000u, 0x093FFu, RPP_OIP_SECTION_DATA_FLASH, 0, false, false}
        }
    };
    return profile;
}

void ValidateTargetProfile(const TargetProfile& profile) {
    if (profile.profile_id.empty() || profile.display_name.empty() ||
        profile.driver_requirement.empty() || profile.regions.empty()) {
        throw std::invalid_argument("target profile metadata is incomplete");
    }
    bool uuid_nonzero = false;
    for (const auto byte : profile.uuid) {
        uuid_nonzero = uuid_nonzero || byte != 0;
    }
    if (!uuid_nonzero) {
        throw std::invalid_argument("target profile UUID is zero");
    }
    for (const auto& region : profile.regions) {
        if (region.start_address > region.end_address || region.section_type == 0) {
            throw std::invalid_argument("target profile contains an invalid memory region");
        }
    }
}

const TargetMemoryRegion& ClassifyTargetAddress(const TargetProfile& profile,
                                                std::uint32_t address) {
    const TargetMemoryRegion* match = nullptr;
    std::uint32_t match_span = (std::numeric_limits<std::uint32_t>::max)();
    for (const auto& region : profile.regions) {
        if (address >= region.start_address && address <= region.end_address) {
            const auto span = region.end_address - region.start_address;
            if (match == nullptr || span < match_span) {
                match = &region;
                match_span = span;
            }
        }
    }
    if (match == nullptr) {
        throw std::runtime_error("image address is outside target profile " + profile.profile_id);
    }
    return *match;
}

} // namespace rl78_programmer

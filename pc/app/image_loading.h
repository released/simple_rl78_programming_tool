#pragma once

#include <filesystem>
#include "rl78_programmer/offline_image.h"

namespace rl78_programmer::app {

struct LoadedTargetImage {
    SRecordImage image;
    OfflineImagePackage package;
    std::uint64_t source_size = 0;
    bool intel_hex_mot = false;
};

LoadedTargetImage LoadTargetImage(const std::filesystem::path& path,
                                  const TargetProfile& profile);

} // namespace rl78_programmer::app

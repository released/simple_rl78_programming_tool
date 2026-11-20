#include "image_loading.h"

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <stdexcept>
#include "rl78_programmer/integrity.h"
#include "rl78_programmer/intel_hex.h"

namespace rl78_programmer::app {

LoadedTargetImage LoadTargetImage(const std::filesystem::path& path,
                                  const TargetProfile& profile) {
    ValidateTargetProfile(profile);
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error("cannot open input file");
    const auto length = stream.tellg();
    // Supported target images are at most 32 KiB. Text records have a bounded
    // expansion allowance; reject before allocating an untrusted file size.
    constexpr std::streamoff maximum_source_size = 4 * 1024 * 1024;
    if (length <= 0 || length > maximum_source_size) {
        throw std::runtime_error("firmware source must contain 1 byte to 4 MiB");
    }
    stream.seekg(0);
    std::vector<std::uint8_t> source(static_cast<std::size_t>(length));
    stream.read(reinterpret_cast<char*>(source.data()), length);
    if (!stream) throw std::runtime_error("failed to read input file");
    LoadedTargetImage result;
    result.source_size = source.size();
    OipBuildOptions options;
    options.target_profile = profile;
    options.project_name = profile.display_name + " Project";
    options.source_name = path.filename().u8string();
    Rpp_Sha256(source.data(), source.size(), options.source_sha256.data());
    auto extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
    if (extension == L".bin") {
        result.image.segments.push_back({0u, std::move(source)});
        result.image.source_record_count = 1u;
    } else {
        const std::string_view text(reinterpret_cast<const char*>(source.data()), source.size());
        result.intel_hex_mot = extension == L".mot" &&
            DetectAddressedTextImageFormat(text) == AddressedTextImageFormat::IntelHex;
        result.image = ParseAddressedTextImage(text);
    }
    result.package = BuildOfflineImagePackage(result.image, options);
    return result;
}

} // namespace rl78_programmer::app

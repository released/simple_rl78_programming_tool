#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

#include "rl78_programmer/srecord.h"

namespace rl78_programmer {

enum class AddressedTextImageFormat {
    MotorolaSRecord,
    IntelHex
};

class IntelHexParseError : public std::runtime_error {
public:
    IntelHexParseError(std::size_t line, const std::string& message);
    [[nodiscard]] std::size_t line() const noexcept { return line_; }

private:
    std::size_t line_;
};

SRecordImage ParseIntelHex(std::string_view text);
SRecordImage LoadIntelHexFile(const std::wstring& path);

// Detects Motorola S-record or Intel HEX from the first meaningful byte.
AddressedTextImageFormat DetectAddressedTextImageFormat(std::string_view text);
const char* AddressedTextImageFormatName(AddressedTextImageFormat format) noexcept;
SRecordImage ParseAddressedTextImage(std::string_view text);
SRecordImage LoadAddressedTextImageFile(const std::wstring& path);

} // namespace rl78_programmer

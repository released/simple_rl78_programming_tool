#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace rl78_programmer {

struct ImageSegment {
    std::uint32_t address = 0;
    std::vector<std::uint8_t> data;
};

struct SRecordImage {
    std::vector<ImageSegment> segments;
    std::uint32_t entry_address = 0;
    bool has_entry_address = false;
    std::size_t source_record_count = 0;

    [[nodiscard]] std::size_t DataSize() const noexcept;
    [[nodiscard]] std::uint32_t LowestAddress() const noexcept;
    [[nodiscard]] std::uint32_t HighestAddressExclusive() const noexcept;
};

class ImageParseError : public std::runtime_error {
public:
    ImageParseError(std::size_t line, const std::string& message);
    [[nodiscard]] std::size_t line() const noexcept { return line_; }

private:
    std::size_t line_;
};

SRecordImage ParseSRecord(std::string_view text);
SRecordImage LoadSRecordFile(const std::wstring& path);

} // namespace rl78_programmer

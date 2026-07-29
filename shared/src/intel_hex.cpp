#include "rl78_programmer/intel_hex.h"

#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>

namespace rl78_programmer {
namespace {

constexpr std::uint32_t kRl78AddressLimit = 0x100000u;

std::string_view RemoveUtf8Bom(std::string_view text) {
    if (text.size() >= 3u &&
        static_cast<unsigned char>(text[0]) == 0xEFu &&
        static_cast<unsigned char>(text[1]) == 0xBBu &&
        static_cast<unsigned char>(text[2]) == 0xBFu) {
        text.remove_prefix(3u);
    }
    return text;
}

std::uint8_t HexNibble(char value, std::size_t line) {
    if (value >= '0' && value <= '9') {
        return static_cast<std::uint8_t>(value - '0');
    }
    if (value >= 'A' && value <= 'F') {
        return static_cast<std::uint8_t>(value - 'A' + 10);
    }
    if (value >= 'a' && value <= 'f') {
        return static_cast<std::uint8_t>(value - 'a' + 10);
    }
    throw IntelHexParseError(line, "invalid hexadecimal character");
}

std::uint8_t HexByte(std::string_view text, std::size_t offset, std::size_t line) {
    if (offset + 2u > text.size()) {
        throw IntelHexParseError(line, "truncated hexadecimal byte");
    }
    return static_cast<std::uint8_t>((HexNibble(text[offset], line) << 4u) |
                                     HexNibble(text[offset + 1u], line));
}

std::uint16_t DataWord(const std::vector<std::uint8_t>& data) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[0]) << 8u) |
                                      data[1]);
}

std::uint32_t DataDword(const std::vector<std::uint8_t>& data) {
    return (static_cast<std::uint32_t>(data[0]) << 24u) |
           (static_cast<std::uint32_t>(data[1]) << 16u) |
           (static_cast<std::uint32_t>(data[2]) << 8u) |
           data[3];
}

void RequireControlRecord(std::uint8_t count,
                          std::uint16_t address,
                          std::uint8_t expected_count,
                          std::size_t line) {
    if (count != expected_count || address != 0u) {
        throw IntelHexParseError(line, "invalid control-record length or address");
    }
}

SRecordImage BuildImage(const std::map<std::uint32_t, std::uint8_t>& bytes,
                        std::size_t source_record_count,
                        bool has_entry_address,
                        std::uint32_t entry_address) {
    SRecordImage image;
    ImageSegment current;
    std::uint32_t expected = 0u;

    image.source_record_count = source_record_count;
    image.has_entry_address = has_entry_address;
    image.entry_address = entry_address;
    for (const auto& item : bytes) {
        if (current.data.empty() || item.first != expected) {
            if (!current.data.empty()) {
                image.segments.push_back(std::move(current));
                current = {};
            }
            current.address = item.first;
        }
        current.data.push_back(item.second);
        expected = item.first + 1u;
    }
    if (!current.data.empty()) {
        image.segments.push_back(std::move(current));
    }
    return image;
}

std::string ReadTextFile(const std::wstring& path, const char* format_name) {
    std::ifstream stream(std::filesystem::path(path), std::ios::binary);
    std::ostringstream buffer;

    if (!stream) {
        throw std::runtime_error(std::string("cannot open ") + format_name + " file");
    }
    buffer << stream.rdbuf();
    if (!stream.good() && !stream.eof()) {
        throw std::runtime_error(std::string("failed to read ") + format_name + " file");
    }
    return buffer.str();
}

} // namespace

IntelHexParseError::IntelHexParseError(std::size_t line, const std::string& message)
    : std::runtime_error("Intel HEX line " + std::to_string(line) + ": " + message),
      line_(line) {}

SRecordImage ParseIntelHex(std::string_view text) {
    std::map<std::uint32_t, std::uint8_t> bytes;
    std::uint32_t base_address = 0u;
    std::uint32_t entry_address = 0u;
    std::size_t source_record_count = 0u;
    std::size_t line_number = 0u;
    std::size_t offset = 0u;
    bool has_entry_address = false;
    bool eof_seen = false;

    text = RemoveUtf8Bom(text);
    while (offset <= text.size()) {
        const std::size_t end = text.find_first_of("\r\n", offset);
        std::string_view line = text.substr(
            offset, end == std::string_view::npos ? text.size() - offset : end - offset);
        ++line_number;
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
            line.remove_suffix(1u);
        }
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
            line.remove_prefix(1u);
        }

        if (!line.empty()) {
            std::uint8_t count;
            std::uint16_t address;
            std::uint8_t type;
            std::vector<std::uint8_t> data;
            std::uint32_t sum;
            std::size_t cursor;

            if (eof_seen) {
                throw IntelHexParseError(line_number, "content follows EOF record");
            }
            if (line.size() < 11u || line.front() != ':') {
                throw IntelHexParseError(line_number, "record must start with ':'");
            }
            count = HexByte(line, 1u, line_number);
            if (line.size() != 11u + static_cast<std::size_t>(count) * 2u) {
                throw IntelHexParseError(line_number, "record length does not match byte count");
            }
            address = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(HexByte(line, 3u, line_number)) << 8u) |
                HexByte(line, 5u, line_number));
            type = HexByte(line, 7u, line_number);
            data.reserve(count);
            sum = static_cast<std::uint32_t>(count) +
                  static_cast<std::uint8_t>(address >> 8u) +
                  static_cast<std::uint8_t>(address) + type;
            cursor = 9u;
            for (std::size_t index = 0u; index < count; ++index) {
                const std::uint8_t value = HexByte(line, cursor, line_number);
                cursor += 2u;
                sum += value;
                data.push_back(value);
            }
            sum += HexByte(line, cursor, line_number);
            if ((sum & 0xFFu) != 0u) {
                throw IntelHexParseError(line_number, "checksum mismatch");
            }

            switch (type) {
            case 0x00u: {
                const std::uint64_t absolute =
                    static_cast<std::uint64_t>(base_address) + address;
                if (absolute >= kRl78AddressLimit ||
                    static_cast<std::uint64_t>(count) > kRl78AddressLimit - absolute) {
                    throw IntelHexParseError(line_number,
                                             "address exceeds RL78 20-bit address space");
                }
                for (std::size_t index = 0u; index < data.size(); ++index) {
                    const std::uint32_t current = static_cast<std::uint32_t>(absolute) +
                                                  static_cast<std::uint32_t>(index);
                    const auto inserted = bytes.emplace(current, data[index]);
                    if (!inserted.second) {
                        if (inserted.first->second != data[index]) {
                            throw IntelHexParseError(
                                line_number, "overlapping records contain different data");
                        }
                        throw IntelHexParseError(line_number, "duplicate address record");
                    }
                }
                ++source_record_count;
                break;
            }
            case 0x01u:
                RequireControlRecord(count, address, 0u, line_number);
                eof_seen = true;
                break;
            case 0x02u:
                RequireControlRecord(count, address, 2u, line_number);
                base_address = static_cast<std::uint32_t>(DataWord(data)) << 4u;
                break;
            case 0x03u: {
                std::uint64_t start;
                RequireControlRecord(count, address, 4u, line_number);
                if (has_entry_address) {
                    throw IntelHexParseError(line_number, "multiple start-address records");
                }
                start = (static_cast<std::uint64_t>(DataWord(data)) << 4u) +
                        static_cast<std::uint16_t>(
                            (static_cast<std::uint16_t>(data[2]) << 8u) | data[3]);
                if (start >= kRl78AddressLimit) {
                    throw IntelHexParseError(line_number,
                                             "start address exceeds RL78 20-bit address space");
                }
                entry_address = static_cast<std::uint32_t>(start);
                has_entry_address = true;
                break;
            }
            case 0x04u:
                RequireControlRecord(count, address, 2u, line_number);
                if (DataWord(data) > 0x000Fu) {
                    throw IntelHexParseError(line_number,
                                             "extended address exceeds RL78 20-bit address space");
                }
                base_address = static_cast<std::uint32_t>(DataWord(data)) << 16u;
                break;
            case 0x05u:
                RequireControlRecord(count, address, 4u, line_number);
                if (has_entry_address) {
                    throw IntelHexParseError(line_number, "multiple start-address records");
                }
                entry_address = DataDword(data);
                if (entry_address >= kRl78AddressLimit) {
                    throw IntelHexParseError(line_number,
                                             "start address exceeds RL78 20-bit address space");
                }
                has_entry_address = true;
                break;
            default:
                throw IntelHexParseError(line_number, "unsupported record type");
            }
        }

        if (end == std::string_view::npos) {
            break;
        }
        offset = end + 1u;
        if (offset < text.size() && text[end] == '\r' && text[offset] == '\n') {
            ++offset;
        }
    }

    if (!eof_seen) {
        throw IntelHexParseError(line_number == 0u ? 1u : line_number,
                                 "missing EOF record");
    }
    if (bytes.empty()) {
        throw IntelHexParseError(line_number == 0u ? 1u : line_number,
                                 "no data records");
    }
    return BuildImage(bytes, source_record_count, has_entry_address, entry_address);
}

SRecordImage LoadIntelHexFile(const std::wstring& path) {
    return ParseIntelHex(ReadTextFile(path, "Intel HEX"));
}

AddressedTextImageFormat DetectAddressedTextImageFormat(std::string_view text) {
    std::string_view normalized = RemoveUtf8Bom(text);
    std::size_t offset = 0u;

    while (offset < normalized.size() &&
           (normalized[offset] == ' ' || normalized[offset] == '\t' ||
            normalized[offset] == '\r' || normalized[offset] == '\n')) {
        ++offset;
    }
    if (offset == normalized.size()) {
        throw std::runtime_error("firmware text image is empty");
    }
    if (normalized[offset] == 'S') {
        return AddressedTextImageFormat::MotorolaSRecord;
    }
    if (normalized[offset] == ':') {
        return AddressedTextImageFormat::IntelHex;
    }
    throw std::runtime_error(
        "unsupported firmware text format: expected Motorola S-record ('S') or Intel HEX (':')");
}

const char* AddressedTextImageFormatName(AddressedTextImageFormat format) noexcept {
    switch (format) {
    case AddressedTextImageFormat::MotorolaSRecord:
        return "Motorola S-record";
    case AddressedTextImageFormat::IntelHex:
        return "Intel HEX";
    default:
        return "unknown text image";
    }
}

SRecordImage ParseAddressedTextImage(std::string_view text) {
    const std::string_view normalized = RemoveUtf8Bom(text);
    const AddressedTextImageFormat format = DetectAddressedTextImageFormat(normalized);

    if (format == AddressedTextImageFormat::MotorolaSRecord) {
        return ParseSRecord(normalized);
    }
    return ParseIntelHex(normalized);
}

SRecordImage LoadAddressedTextImageFile(const std::wstring& path) {
    return ParseAddressedTextImage(ReadTextFile(path, "firmware text image"));
}

} // namespace rl78_programmer

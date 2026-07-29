#include "rl78_programmer/srecord.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>

namespace rl78_programmer {
namespace {

std::uint8_t HexNibble(char c, std::size_t line) {
    if (c >= '0' && c <= '9') {
        return static_cast<std::uint8_t>(c - '0');
    }
    if (c >= 'A' && c <= 'F') {
        return static_cast<std::uint8_t>(c - 'A' + 10);
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<std::uint8_t>(c - 'a' + 10);
    }
    throw ImageParseError(line, "invalid hexadecimal character");
}

std::uint8_t HexByte(std::string_view text, std::size_t offset, std::size_t line) {
    if (offset + 2 > text.size()) {
        throw ImageParseError(line, "truncated hexadecimal byte");
    }
    return static_cast<std::uint8_t>((HexNibble(text[offset], line) << 4) |
                                     HexNibble(text[offset + 1], line));
}

std::size_t AddressBytes(char type, std::size_t line) {
    switch (type) {
    case '0': case '1': case '5': case '9': return 2;
    case '2': case '6': case '8': return 3;
    case '3': case '7': return 4;
    default: throw ImageParseError(line, "unsupported S-record type");
    }
}

bool IsDataRecord(char type) {
    return type == '1' || type == '2' || type == '3';
}

bool IsTerminationRecord(char type) {
    return type == '7' || type == '8' || type == '9';
}

} // namespace

std::size_t SRecordImage::DataSize() const noexcept {
    std::size_t size = 0;
    for (const auto& segment : segments) {
        size += segment.data.size();
    }
    return size;
}

std::uint32_t SRecordImage::LowestAddress() const noexcept {
    return segments.empty() ? 0u : segments.front().address;
}

std::uint32_t SRecordImage::HighestAddressExclusive() const noexcept {
    if (segments.empty()) {
        return 0u;
    }
    const auto& last = segments.back();
    return last.address + static_cast<std::uint32_t>(last.data.size());
}

ImageParseError::ImageParseError(std::size_t line, const std::string& message)
    : std::runtime_error("S-record line " + std::to_string(line) + ": " + message), line_(line) {}

SRecordImage ParseSRecord(std::string_view text) {
    std::map<std::uint32_t, std::uint8_t> bytes;
    SRecordImage image;
    std::size_t line_number = 0;
    bool terminated = false;
    std::size_t offset = 0;

    while (offset <= text.size()) {
        const std::size_t end = text.find_first_of("\r\n", offset);
        std::string_view line = text.substr(offset, end == std::string_view::npos ? text.size() - offset : end - offset);
        ++line_number;
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
            line.remove_suffix(1);
        }
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
            line.remove_prefix(1);
        }

        if (!line.empty()) {
            if (terminated) {
                throw ImageParseError(line_number, "content follows termination record");
            }
            if (line.size() < 4 || line[0] != 'S') {
                throw ImageParseError(line_number, "record must start with S and a type");
            }
            const char type = line[1];
            const std::size_t address_bytes = AddressBytes(type, line_number);
            const std::uint8_t count = HexByte(line, 2, line_number);
            if (count < address_bytes + 1) {
                throw ImageParseError(line_number, "byte count is smaller than address and checksum");
            }
            if (line.size() != 4 + static_cast<std::size_t>(count) * 2) {
                throw ImageParseError(line_number, "record length does not match byte count");
            }

            std::uint32_t sum = count;
            std::uint32_t address = 0;
            std::size_t cursor = 4;
            for (std::size_t i = 0; i < address_bytes; ++i) {
                const std::uint8_t value = HexByte(line, cursor, line_number);
                cursor += 2;
                sum += value;
                address = (address << 8) | value;
            }
            const std::size_t data_bytes = count - address_bytes - 1;
            std::vector<std::uint8_t> record_data;
            record_data.reserve(data_bytes);
            for (std::size_t i = 0; i < data_bytes; ++i) {
                const std::uint8_t value = HexByte(line, cursor, line_number);
                cursor += 2;
                sum += value;
                record_data.push_back(value);
            }
            const std::uint8_t checksum = HexByte(line, cursor, line_number);
            sum += checksum;
            if ((sum & 0xFFu) != 0xFFu) {
                throw ImageParseError(line_number, "checksum mismatch");
            }

            if (IsDataRecord(type)) {
                if (address > 0xFFFFFu || data_bytes > (0x100000u - address)) {
                    throw ImageParseError(line_number, "address exceeds RL78 20-bit address space");
                }
                for (std::size_t i = 0; i < record_data.size(); ++i) {
                    const std::uint32_t current = address + static_cast<std::uint32_t>(i);
                    const auto inserted = bytes.emplace(current, record_data[i]);
                    if (!inserted.second) {
                        if (inserted.first->second != record_data[i]) {
                            throw ImageParseError(line_number, "overlapping records contain different data");
                        }
                        throw ImageParseError(line_number, "duplicate address record");
                    }
                }
                ++image.source_record_count;
            } else if (IsTerminationRecord(type)) {
                image.entry_address = address;
                image.has_entry_address = true;
                terminated = true;
            }
        }

        if (end == std::string_view::npos) {
            break;
        }
        offset = end + 1;
        if (offset < text.size() && text[end] == '\r' && text[offset] == '\n') {
            ++offset;
        }
    }

    if (bytes.empty()) {
        throw ImageParseError(line_number == 0 ? 1 : line_number, "no data records");
    }

    ImageSegment current;
    std::uint32_t expected = 0;
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

SRecordImage LoadSRecordFile(const std::wstring& path) {
    std::ifstream stream(std::filesystem::path(path), std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open S-record file");
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    if (!stream.good() && !stream.eof()) {
        throw std::runtime_error("failed to read S-record file");
    }
    return ParseSRecord(buffer.str());
}

} // namespace rl78_programmer

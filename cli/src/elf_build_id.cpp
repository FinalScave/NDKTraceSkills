#include "ndktrace/elf_build_id.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <vector>

namespace ndktrace {
namespace {

constexpr unsigned char kElfMagic0 = 0x7f;
constexpr unsigned char kElfMagic1 = 'E';
constexpr unsigned char kElfMagic2 = 'L';
constexpr unsigned char kElfMagic3 = 'F';
constexpr unsigned char kElfClass32 = 1;
constexpr unsigned char kElfClass64 = 2;
constexpr unsigned char kElfData2Lsb = 1;
constexpr unsigned char kElfData2Msb = 2;
constexpr std::uint32_t kProgramHeaderNote = 4;
constexpr std::uint32_t kSectionHeaderNote = 7;
constexpr std::uint32_t kGnuBuildIdType = 3;

struct ElfDescriptor {
    bool is_64_bit = false;
    bool little_endian = true;
    const std::vector<unsigned char>* bytes = nullptr;
};

std::optional<std::uint16_t> ReadUint16(const ElfDescriptor& elf, std::size_t offset) {
    if (elf.bytes == nullptr || offset + 2 > elf.bytes->size()) {
        return std::nullopt;
    }

    const auto& bytes = *elf.bytes;
    if (elf.little_endian) {
        return static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(bytes[offset]) |
            (static_cast<std::uint16_t>(bytes[offset + 1]) << 8));
    }

    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[offset]) << 8) |
        static_cast<std::uint16_t>(bytes[offset + 1]));
}

std::optional<std::uint32_t> ReadUint32(const ElfDescriptor& elf, std::size_t offset) {
    if (elf.bytes == nullptr || offset + 4 > elf.bytes->size()) {
        return std::nullopt;
    }

    const auto& bytes = *elf.bytes;
    if (elf.little_endian) {
        return static_cast<std::uint32_t>(bytes[offset]) |
            (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
            (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
            (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
    }

    return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
        static_cast<std::uint32_t>(bytes[offset + 3]);
}

std::optional<std::uint64_t> ReadUint64(const ElfDescriptor& elf, std::size_t offset) {
    if (elf.bytes == nullptr || offset + 8 > elf.bytes->size()) {
        return std::nullopt;
    }

    const auto& bytes = *elf.bytes;
    std::uint64_t value = 0;
    if (elf.little_endian) {
        for (int i = 7; i >= 0; --i) {
            value = (value << 8) | bytes[offset + static_cast<std::size_t>(i)];
        }
    } else {
        for (int i = 0; i < 8; ++i) {
            value = (value << 8) | bytes[offset + static_cast<std::size_t>(i)];
        }
    }
    return value;
}

std::string TrimNoteName(const std::vector<unsigned char>& bytes, std::size_t offset, std::size_t size) {
    if (offset + size > bytes.size()) {
        return {};
    }

    std::size_t effective_size = size;
    while (effective_size > 0 && bytes[offset + effective_size - 1] == 0) {
        --effective_size;
    }
    return std::string(reinterpret_cast<const char*>(bytes.data() + offset), effective_size);
}

std::string BytesToHex(const std::vector<unsigned char>& bytes, std::size_t offset, std::size_t size) {
    static constexpr char kHexDigits[] = "0123456789abcdef";
    if (offset + size > bytes.size()) {
        return {};
    }

    std::string hex;
    hex.reserve(size * 2);
    for (std::size_t index = 0; index < size; ++index) {
        const unsigned char value = bytes[offset + index];
        hex.push_back(kHexDigits[value >> 4]);
        hex.push_back(kHexDigits[value & 0x0f]);
    }
    return hex;
}

std::size_t Align4(std::size_t value) {
    return (value + 3U) & ~std::size_t(3U);
}

std::optional<std::string> ParseNoteBlock(
    const ElfDescriptor& elf,
    std::size_t offset,
    std::size_t size) {
    if (elf.bytes == nullptr) {
        return std::nullopt;
    }

    const auto& bytes = *elf.bytes;
    if (offset > bytes.size() || size > bytes.size() - offset) {
        return std::nullopt;
    }

    const std::size_t end = offset + size;
    std::size_t cursor = offset;
    while (cursor + 12 <= end) {
        const auto name_size = ReadUint32(elf, cursor);
        const auto description_size = ReadUint32(elf, cursor + 4);
        const auto note_type = ReadUint32(elf, cursor + 8);
        if (!name_size.has_value() || !description_size.has_value() || !note_type.has_value()) {
            return std::nullopt;
        }

        cursor += 12;
        const std::size_t aligned_name_size = Align4(*name_size);
        const std::size_t aligned_description_size = Align4(*description_size);
        if (cursor > end || aligned_name_size > end - cursor) {
            return std::nullopt;
        }

        const std::string note_name = TrimNoteName(bytes, cursor, *name_size);
        const std::size_t description_offset = cursor + aligned_name_size;
        if (description_offset > end || aligned_description_size > end - description_offset) {
            return std::nullopt;
        }

        if (*note_type == kGnuBuildIdType && note_name == "GNU") {
            return BytesToHex(bytes, description_offset, *description_size);
        }

        cursor = description_offset + aligned_description_size;
    }

    return std::nullopt;
}

std::optional<std::string> ReadBuildIdFromProgramHeaders(const ElfDescriptor& elf) {
    const std::size_t program_header_offset_field = elf.is_64_bit ? 32 : 28;
    const std::size_t program_header_entry_size_field = elf.is_64_bit ? 54 : 42;
    const std::size_t program_header_count_field = elf.is_64_bit ? 56 : 44;

    const auto program_header_offset = elf.is_64_bit
        ? ReadUint64(elf, program_header_offset_field)
        : std::optional<std::uint64_t>(ReadUint32(elf, program_header_offset_field).value_or(0));
    const auto program_header_entry_size = ReadUint16(elf, program_header_entry_size_field);
    const auto program_header_count = ReadUint16(elf, program_header_count_field);
    if (!program_header_offset.has_value() ||
        !program_header_entry_size.has_value() ||
        !program_header_count.has_value()) {
        return std::nullopt;
    }

    for (std::uint16_t index = 0; index < *program_header_count; ++index) {
        const std::size_t header_offset = static_cast<std::size_t>(*program_header_offset) +
            static_cast<std::size_t>(index) * static_cast<std::size_t>(*program_header_entry_size);
        const auto header_type = ReadUint32(elf, header_offset);
        if (!header_type.has_value() || *header_type != kProgramHeaderNote) {
            continue;
        }

        const auto note_offset = elf.is_64_bit
            ? ReadUint64(elf, header_offset + 8)
            : std::optional<std::uint64_t>(ReadUint32(elf, header_offset + 4).value_or(0));
        const auto note_size = elf.is_64_bit
            ? ReadUint64(elf, header_offset + 32)
            : std::optional<std::uint64_t>(ReadUint32(elf, header_offset + 16).value_or(0));
        if (!note_offset.has_value() || !note_size.has_value()) {
            continue;
        }

        if (const auto build_id = ParseNoteBlock(
                elf,
                static_cast<std::size_t>(*note_offset),
                static_cast<std::size_t>(*note_size))) {
            return build_id;
        }
    }

    return std::nullopt;
}

std::optional<std::string> ReadBuildIdFromSectionHeaders(const ElfDescriptor& elf) {
    const std::size_t section_header_offset_field = elf.is_64_bit ? 40 : 32;
    const std::size_t section_header_entry_size_field = elf.is_64_bit ? 58 : 46;
    const std::size_t section_header_count_field = elf.is_64_bit ? 60 : 48;

    const auto section_header_offset = elf.is_64_bit
        ? ReadUint64(elf, section_header_offset_field)
        : std::optional<std::uint64_t>(ReadUint32(elf, section_header_offset_field).value_or(0));
    const auto section_header_entry_size = ReadUint16(elf, section_header_entry_size_field);
    const auto section_header_count = ReadUint16(elf, section_header_count_field);
    if (!section_header_offset.has_value() ||
        !section_header_entry_size.has_value() ||
        !section_header_count.has_value()) {
        return std::nullopt;
    }

    for (std::uint16_t index = 0; index < *section_header_count; ++index) {
        const std::size_t header_offset = static_cast<std::size_t>(*section_header_offset) +
            static_cast<std::size_t>(index) * static_cast<std::size_t>(*section_header_entry_size);
        const auto section_type = ReadUint32(elf, header_offset + 4);
        if (!section_type.has_value() || *section_type != kSectionHeaderNote) {
            continue;
        }

        const auto note_offset = elf.is_64_bit
            ? ReadUint64(elf, header_offset + 24)
            : std::optional<std::uint64_t>(ReadUint32(elf, header_offset + 16).value_or(0));
        const auto note_size = elf.is_64_bit
            ? ReadUint64(elf, header_offset + 32)
            : std::optional<std::uint64_t>(ReadUint32(elf, header_offset + 20).value_or(0));
        if (!note_offset.has_value() || !note_size.has_value()) {
            continue;
        }

        if (const auto build_id = ParseNoteBlock(
                elf,
                static_cast<std::size_t>(*note_offset),
                static_cast<std::size_t>(*note_size))) {
            return build_id;
        }
    }

    return std::nullopt;
}

}  // namespace

std::optional<std::string> ReadElfBuildId(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }

    std::vector<unsigned char> bytes;
    bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    if (bytes.size() < 16) {
        return std::nullopt;
    }
    if (bytes[0] != kElfMagic0 || bytes[1] != kElfMagic1 || bytes[2] != kElfMagic2 || bytes[3] != kElfMagic3) {
        return std::nullopt;
    }

    const unsigned char elf_class = bytes[4];
    const unsigned char elf_data = bytes[5];
    if ((elf_class != kElfClass32 && elf_class != kElfClass64) ||
        (elf_data != kElfData2Lsb && elf_data != kElfData2Msb)) {
        return std::nullopt;
    }

    ElfDescriptor elf;
    elf.is_64_bit = elf_class == kElfClass64;
    elf.little_endian = elf_data == kElfData2Lsb;
    elf.bytes = &bytes;

    if (const auto build_id = ReadBuildIdFromProgramHeaders(elf)) {
        return build_id;
    }
    return ReadBuildIdFromSectionHeaders(elf);
}

}  // namespace ndktrace

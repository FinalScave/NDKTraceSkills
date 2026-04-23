#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "ndktrace/elf_build_id.h"
#include "ndktrace/symbol_file_resolver.h"

namespace {

using ndktrace::ReadElfBuildId;
using ndktrace::ResolveSymbolFile;
using ndktrace::SymbolFileLookupRequest;
using ndktrace::SymbolFileLookupResult;

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(1);
    }
}

void WriteUint16(std::vector<unsigned char>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<unsigned char>(value & 0xff);
    bytes[offset + 1] = static_cast<unsigned char>((value >> 8) & 0xff);
}

void WriteUint32(std::vector<unsigned char>& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<unsigned char>(value & 0xff);
    bytes[offset + 1] = static_cast<unsigned char>((value >> 8) & 0xff);
    bytes[offset + 2] = static_cast<unsigned char>((value >> 16) & 0xff);
    bytes[offset + 3] = static_cast<unsigned char>((value >> 24) & 0xff);
}

void WriteUint64(std::vector<unsigned char>& bytes, std::size_t offset, std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index) {
        bytes[offset + index] = static_cast<unsigned char>((value >> (index * 8)) & 0xff);
    }
}

std::filesystem::path CreateTempRoot() {
    const auto unique_suffix = std::to_string(
        std::chrono::system_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path() / ("ndktrace_symbol_tests_" + unique_suffix);
    std::filesystem::create_directories(root);
    return root;
}

void WriteElfFileWithBuildId(
    const std::filesystem::path& path,
    const std::vector<unsigned char>& build_id_bytes) {
    constexpr std::size_t elf_header_size = 64;
    constexpr std::size_t program_header_offset = elf_header_size;
    constexpr std::size_t program_header_size = 56;
    constexpr std::size_t note_offset = 0x100;
    constexpr std::size_t note_header_size = 12;
    constexpr std::size_t note_name_size = 4;

    const std::size_t note_size = note_header_size + note_name_size + build_id_bytes.size();
    std::vector<unsigned char> bytes(note_offset + note_size, 0);
    bytes[0] = 0x7f;
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    bytes[4] = 2;
    bytes[5] = 1;
    bytes[6] = 1;

    WriteUint16(bytes, 16, 3);
    WriteUint16(bytes, 18, 62);
    WriteUint32(bytes, 20, 1);
    WriteUint64(bytes, 32, program_header_offset);
    WriteUint16(bytes, 52, elf_header_size);
    WriteUint16(bytes, 54, program_header_size);
    WriteUint16(bytes, 56, 1);

    WriteUint32(bytes, program_header_offset, 4);
    WriteUint64(bytes, program_header_offset + 8, note_offset);
    WriteUint64(bytes, program_header_offset + 32, note_size);
    WriteUint64(bytes, program_header_offset + 40, note_size);
    WriteUint64(bytes, program_header_offset + 48, 4);

    WriteUint32(bytes, note_offset, note_name_size);
    WriteUint32(bytes, note_offset + 4, static_cast<std::uint32_t>(build_id_bytes.size()));
    WriteUint32(bytes, note_offset + 8, 3);
    bytes[note_offset + 12] = 'G';
    bytes[note_offset + 13] = 'N';
    bytes[note_offset + 14] = 'U';
    bytes[note_offset + 15] = '\0';

    for (std::size_t index = 0; index < build_id_bytes.size(); ++index) {
        bytes[note_offset + 16 + index] = build_id_bytes[index];
    }

    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void WriteTextFile(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << text;
}

void TestReadElfBuildId() {
    const auto root = CreateTempRoot();
    const auto scope_exit = [&root]() {
        std::filesystem::remove_all(root);
    };

    const auto elf_path = root / "libfoo.so";
    WriteElfFileWithBuildId(elf_path, {0xde, 0xad, 0xbe, 0xef});

    const auto build_id = ReadElfBuildId(elf_path);
    Expect(build_id.has_value(), "Expected ELF BuildId to be parsed.");
    Expect(*build_id == "deadbeef", "Expected BuildId hex string.");

    scope_exit();
}

void TestReadElfBuildIdRejectsTextFile() {
    const auto root = CreateTempRoot();
    const auto scope_exit = [&root]() {
        std::filesystem::remove_all(root);
    };

    const auto text_path = root / "libfoo.so";
    WriteTextFile(text_path, "not an elf");

    Expect(!ReadElfBuildId(text_path).has_value(), "Expected non-ELF files to be rejected.");

    scope_exit();
}

void TestResolveSymbolFilePrefersBuildId() {
    const auto root = CreateTempRoot();
    const auto scope_exit = [&root]() {
        std::filesystem::remove_all(root);
    };

    const auto wrong_path = root / "arm64-v8a" / "libfoo.so";
    const auto correct_path = root / "symbols" / "libfoo.so";
    WriteElfFileWithBuildId(wrong_path, {0xde, 0xad, 0xbe, 0xef});
    WriteElfFileWithBuildId(correct_path, {0xca, 0xfe, 0xba, 0xbe});

    const SymbolFileLookupResult result = ResolveSymbolFile({
        root,
        "libfoo.so",
        "/data/app/com.example/lib/arm64/libfoo.so",
        "cafebabe",
        true,
        "basename"});

    Expect(result.path.has_value(), "Expected a BuildId match to be selected.");
    Expect(*result.path == correct_path, "Expected BuildId match to win over the first basename hit.");
    Expect(result.strategy == "build_id", "Expected build_id strategy.");
    Expect(result.file_build_id == "cafebabe", "Expected resolved BuildId to be preserved.");

    scope_exit();
}

void TestResolveSymbolFileRejectsKnownMismatch() {
    const auto root = CreateTempRoot();
    const auto scope_exit = [&root]() {
        std::filesystem::remove_all(root);
    };

    const auto wrong_path = root / "libfoo.so";
    WriteElfFileWithBuildId(wrong_path, {0xde, 0xad, 0xbe, 0xef});

    const SymbolFileLookupResult result = ResolveSymbolFile({
        root,
        "libfoo.so",
        "/data/app/com.example/lib/arm64/libfoo.so",
        "cafebabe",
        true,
        "basename"});

    Expect(!result.path.has_value(), "Expected known BuildId mismatches to be rejected.");
    Expect(result.had_build_id_mismatch, "Expected mismatch flag when only wrong BuildId candidates exist.");

    scope_exit();
}

void TestResolveSymbolFileFallsBackToExactPathWithoutBuildId() {
    const auto root = CreateTempRoot();
    const auto scope_exit = [&root]() {
        std::filesystem::remove_all(root);
    };

    const auto candidate = root / "symbols" / "system" / "lib64" / "libfoo.so";
    WriteTextFile(candidate, "placeholder");

    const SymbolFileLookupResult result = ResolveSymbolFile({
        root,
        "libfoo.so",
        "/system/lib64/libfoo.so",
        "cafebabe",
        true,
        "exact"});

    Expect(result.path.has_value(), "Expected exact path fallback when BuildId cannot be verified.");
    Expect(*result.path == candidate, "Expected exact path candidate to be selected.");
    Expect(result.strategy == "exact_path", "Expected exact_path fallback strategy.");
    Expect(result.file_build_id.empty(), "Expected empty BuildId for non-ELF fallback file.");

    scope_exit();
}

}  // namespace

int main() {
    TestReadElfBuildId();
    TestReadElfBuildIdRejectsTextFile();
    TestResolveSymbolFilePrefersBuildId();
    TestResolveSymbolFileRejectsKnownMismatch();
    TestResolveSymbolFileFallsBackToExactPathWithoutBuildId();
    return 0;
}

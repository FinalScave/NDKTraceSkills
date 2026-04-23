#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "ndktrace/crash_artifact_parser.h"
#include "ndktrace/parser.h"

namespace {

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(1);
    }
}

std::string ReadFixture(const std::string& name) {
    const std::filesystem::path path = std::filesystem::path(NDKTRACE_TEST_DATA_DIR) / name;
    std::ifstream input(path, std::ios::binary);
    Expect(static_cast<bool>(input), "Expected fixture file to exist: " + path.string());
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

void TestFullTombstoneFixture() {
    const auto parsed = ndktrace::ParseCrashArtifactText(ReadFixture("full_tombstone.txt"));

    Expect(parsed.artifact.abi == "arm64", "Expected ABI from tombstone fixture.");
    Expect(
        parsed.artifact.signal == "signal 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault addr 0xc",
        "Expected signal from tombstone fixture.");
    Expect(
        parsed.artifact.tombstone_path == "/data/tombstones/tombstone_06",
        "Expected tombstone path from fixture.");
    Expect(parsed.artifact.has_backtrace, "Expected backtrace section flag.");
    Expect(parsed.artifact.has_memory_map, "Expected memory map section flag.");
    Expect(parsed.frame_lines.size() == 3, "Expected three backtrace frame lines.");
    Expect(parsed.memory_maps.size() == 2, "Expected two memory map entries.");
    Expect(
        parsed.memory_maps[0].image_path == "/apex/com.android.runtime/lib64/bionic/libc.so",
        "Expected first memory map image path.");
    Expect(parsed.memory_maps[0].build_id == "3b5c4d7e8a9f0123", "Expected first memory map BuildId.");
    Expect(parsed.memory_maps[0].load_base == "0x7b2c400000", "Expected first memory map load base.");
}

void TestFallbackFramesWithoutBacktraceSection() {
    const auto parsed = ndktrace::ParseCrashArtifactText(
        "#00 pc 000000000001d4b8 /apex/com.android.runtime/lib64/bionic/libc.so (abort+164)\n"
        "#01 pc 0000000000059f88 /apex/com.android.runtime/lib64/bionic/libart.so (art_quick_invoke_stub+584)\n");

    Expect(!parsed.artifact.has_backtrace, "Expected no backtrace section flag.");
    Expect(parsed.frame_lines.size() == 2, "Expected fallback frame lines when no section header exists.");
}

void TestLogcatFixture() {
    const auto parsed = ndktrace::ParseCrashArtifactText(ReadFixture("android_logcat.txt"));

    Expect(
        parsed.artifact.signal ==
            "Fatal signal 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault addr 0x1 in tid 4038 (weeteditor.demo), pid 4038 (weeteditor.demo)",
        "Expected fatal signal summary from logcat fixture.");
    Expect(!parsed.artifact.has_backtrace, "Expected no explicit backtrace header in logcat fixture.");
    Expect(parsed.frame_lines.size() == 7, "Expected seven frame lines extracted from logcat fixture.");
    Expect(
        parsed.frame_lines[0].text.rfind("#00 pc 00000000001a6614", 0) == 0,
        "Expected first extracted frame payload.");

    const auto first_frame = ndktrace::ParseFrameLine(parsed.frame_lines[0].text);
    Expect(first_frame.matched, "Expected first logcat frame payload to parse.");
    Expect(
        first_frame.build_id == "152ef1ea72b6197f20ca373ca45dc4b7b84618b3",
        "Expected BuildId to survive logcat payload extraction.");
}

}  // namespace

int main() {
    TestFullTombstoneFixture();
    TestFallbackFramesWithoutBacktraceSection();
    TestLogcatFixture();
    return 0;
}

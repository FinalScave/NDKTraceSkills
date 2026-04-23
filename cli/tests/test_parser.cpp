#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "ndktrace/json_output.h"
#include "ndktrace/parser.h"

namespace {

using ndktrace::FrameResult;
using ndktrace::ParsedImageMetadata;
using ndktrace::ParseFrameLine;
using ndktrace::ParseMemoryMapLine;
using ndktrace::RestoreResult;
using ndktrace::ToJson;

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
    std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return content;
}

void TestTombstoneFrameWithHint() {
    const auto parsed = ParseFrameLine(
        "#00 pc 000000000001d4b8 /apex/com.android.runtime/lib64/bionic/libc.so (abort+164) (BuildId: deadbeef)");
    Expect(parsed.matched, "Expected tombstone frame to match.");
    Expect(parsed.frame_number == 0, "Expected frame number to be captured.");
    Expect(parsed.address == "000000000001d4b8", "Expected tombstone address to be captured.");
    Expect(parsed.library_name == "libc.so", "Expected library name to be captured.");
    Expect(parsed.frame_kind == "shared_object", "Expected shared object frame kind.");
    Expect(parsed.frame_suffix == "(abort+164) (BuildId: deadbeef)", "Expected suffix to be preserved.");
    Expect(parsed.symbol_hint == "abort+164", "Expected symbol hint to be captured.");
    Expect(parsed.build_id == "deadbeef", "Expected BuildId to be captured.");
}

void TestBacktraceFrameWithoutPcToken() {
    const auto parsed = ParseFrameLine(
        "#03 0x0000007f6b2d1234 /data/app/com.example/lib/arm64/libfoo.so (demo::Crash()+24)");
    Expect(parsed.matched, "Expected backtrace frame without pc token to match.");
    Expect(parsed.frame_number == 3, "Expected frame number without pc token.");
    Expect(parsed.address == "0x0000007f6b2d1234", "Expected 0x address to be captured.");
    Expect(parsed.library_name == "libfoo.so", "Expected basename lookup to be preserved.");
    Expect(parsed.symbol_hint == "demo::Crash()+24", "Expected inline symbol hint to be captured.");
}

void TestCompactFrame() {
    const auto parsed = ParseFrameLine("pc 0005a6c8 /system/lib/libc.so");
    Expect(parsed.matched, "Expected compact frame to match.");
    Expect(parsed.library_path == "/system/lib/libc.so", "Expected compact library path.");
}

void TestApkFrameClassification() {
    const auto parsed = ParseFrameLine(
        "#12 pc 00000000000abcde /system/priv-app/Velvet/Velvet.apk (offset 0x1234000) (SomeMethod+176)");
    Expect(parsed.matched, "Expected APK-backed frame to match.");
    Expect(parsed.frame_kind == "apk", "Expected APK frame kind.");
    Expect(parsed.symbol_hint == "SomeMethod+176", "Expected symbol hint after offset suffix.");
    Expect(parsed.file_offset == "0x1234000", "Expected APK offset to be captured.");
}

void TestDexFrameClassification() {
    const auto parsed = ParseFrameLine(
        "#07 pc 0000000000011111 /data/app/com.example/base.vdex/classes.dex (Example.run+88)");
    Expect(parsed.matched, "Expected dex frame to match.");
    Expect(parsed.frame_kind == "dex", "Expected dex frame kind.");
}

void TestLoadBaseExtraction() {
    const auto parsed = ParseFrameLine(
        "#05 pc 0000000000559f88 /system/lib64/libart.so (BuildId: cafebabe) (load base 0x2000) (art_quick_invoke_stub+584)");
    Expect(parsed.matched, "Expected load base frame to match.");
    Expect(parsed.build_id == "cafebabe", "Expected BuildId from suffix.");
    Expect(parsed.load_base == "0x2000", "Expected load base from suffix.");
    Expect(parsed.symbol_hint == "art_quick_invoke_stub+584", "Expected symbol hint after load base.");
}

void TestSharedLibraryFixture() {
    const auto lines = ndktrace::SplitLines(ReadFixture("shared_library_frames.txt"));
    Expect(lines.size() >= 4, "Expected shared library fixture lines.");

    const auto parsed = ParseFrameLine(lines[1]);
    Expect(parsed.matched, "Expected shared library fixture frame to parse.");
    Expect(parsed.frame_kind == "shared_object", "Expected shared library frame kind from fixture.");
    Expect(parsed.build_id == "3b5c4d7e8a9f0123", "Expected shared library BuildId from fixture.");
}

void TestApkFixture() {
    const auto lines = ndktrace::SplitLines(ReadFixture("apk_offset_frames.txt"));
    Expect(lines.size() >= 4, "Expected APK fixture lines.");

    const auto parsed = ParseFrameLine(lines[1]);
    Expect(parsed.matched, "Expected APK fixture frame to parse.");
    Expect(parsed.frame_kind == "apk", "Expected APK frame kind from fixture.");
    Expect(parsed.file_offset == "0x1234000", "Expected APK file offset from fixture.");
}

void TestDexFixture() {
    const auto lines = ndktrace::SplitLines(ReadFixture("dex_frames.txt"));
    Expect(lines.size() >= 4, "Expected dex fixture lines.");

    const auto parsed = ParseFrameLine(lines[1]);
    Expect(parsed.matched, "Expected dex fixture frame to parse.");
    Expect(parsed.frame_kind == "dex", "Expected dex frame kind from fixture.");
    Expect(parsed.symbol_hint == "com.example.app.MainActivity.onCreate+88", "Expected dex symbol hint from fixture.");
}

void TestMemoryMapFixture() {
    const auto lines = ndktrace::SplitLines(ReadFixture("memory_map_buildid_load_base.txt"));
    Expect(lines.size() >= 6, "Expected memory map fixture lines.");

    ParsedImageMetadata image = ParseMemoryMapLine(lines[1]);
    Expect(image.matched, "Expected memory map line to parse.");
    Expect(image.image_path == "/apex/com.android.runtime/lib64/bionic/libc.so", "Expected memory map path.");
    Expect(image.file_offset == "0x00000000", "Expected memory map file offset.");

    ndktrace::ApplyMemoryMapMetadataLine(lines[2], image);
    ndktrace::ApplyMemoryMapMetadataLine(lines[3], image);
    Expect(image.build_id == "3b5c4d7e8a9f0123", "Expected memory map BuildId.");
    Expect(image.load_base == "0x7b2c400000", "Expected memory map load base.");
}

void TestArtifactHeaderParsing() {
    Expect(
        ndktrace::ExtractAbiValue("ABI: 'arm64'") == "arm64",
        "Expected ABI value to be extracted.");
    Expect(
        ndktrace::ExtractSignalSummary("signal 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault addr 0xc") ==
            "signal 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault addr 0xc",
        "Expected signal summary to be extracted.");
    Expect(
        ndktrace::ExtractSignalSummary("Fatal signal 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault addr 0x1") ==
            "Fatal signal 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault addr 0x1",
        "Expected fatal signal summary to be extracted.");
    Expect(
        ndktrace::ExtractTombstonePath("Tombstone written to: /data/tombstones/tombstone_06") ==
            "/data/tombstones/tombstone_06",
        "Expected tombstone path to be extracted.");
}

void TestUnmatchedLine() {
    const auto parsed = ParseFrameLine("signal 11 (SIGSEGV), code 1 (SEGV_MAPERR)");
    Expect(!parsed.matched, "Expected signal summary line to stay unmatched.");
}

void TestSymbolizerOutputParsing() {
    FrameResult frame;
    ndktrace::ApplyToolOutput("demo::Crash()\n/src/demo.cpp:42:7", "llvm-symbolizer", frame);
    Expect(frame.function_name == "demo::Crash()", "Expected symbolizer function name.");
    Expect(frame.source_file == "/src/demo.cpp", "Expected symbolizer source file.");
    Expect(frame.source_line == 42, "Expected symbolizer source line.");
    Expect(frame.source_column == 7, "Expected symbolizer source column.");
}

void TestWindowsSymbolizerOutputParsing() {
    FrameResult frame;
    ndktrace::ApplyToolOutput(
        "demo::Crash()\nD:/Projects/Example/src/demo.cpp:1197:39",
        "llvm-symbolizer",
        frame);
    Expect(frame.function_name == "demo::Crash()", "Expected Windows symbolizer function name.");
    Expect(
        frame.source_file == "D:/Projects/Example/src/demo.cpp",
        "Expected Windows symbolizer source file.");
    Expect(frame.source_line == 1197, "Expected Windows symbolizer source line.");
    Expect(frame.source_column == 39, "Expected Windows symbolizer source column.");
}

void TestAddr2lineOutputParsing() {
    FrameResult frame;
    ndktrace::ApplyToolOutput("demo::Crash() at /src/demo.cpp:42", "llvm-addr2line", frame);
    Expect(frame.function_name == "demo::Crash()", "Expected addr2line function name.");
    Expect(frame.source_file == "/src/demo.cpp", "Expected addr2line source file.");
    Expect(frame.source_line == 42, "Expected addr2line source line.");
    Expect(frame.source_column == -1, "Expected no addr2line source column.");
}

void TestJsonContract() {
    RestoreResult result;
    result.ok = true;
    result.request.ndk_path = "D:/Android/Sdk/ndk/26.1.10909125";
    result.request.so_path = "D:/symbols";
    result.request.read_stdin = true;
    result.artifact.abi = "arm64";
    result.artifact.signal = "signal 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault addr 0xc";
    result.artifact.tombstone_path = "/data/tombstones/tombstone_06";
    result.artifact.has_backtrace = true;
    result.artifact.has_memory_map = true;
    result.toolchain.ndk_path = result.request.ndk_path;
    result.toolchain.selected_tool = "llvm-symbolizer";
    result.summary.frames_total = 1;
    result.summary.frames_matched = 1;
    result.summary.frames_resolved = 1;

    FrameResult frame;
    frame.index = 0;
    frame.raw_line = "#00 pc 0005a6c8 /system/lib/libc.so (abort+164)";
    frame.matched = true;
    frame.status = "resolved";
    frame.frame_number = 0;
    frame.frame_kind = "shared_object";
    frame.frame_suffix = "(abort+164)";
    frame.address = "0x0005a6c8";
    frame.library_name = "libc.so";
    frame.library_path = "/system/lib/libc.so";
    frame.symbol_hint = "abort+164";
    frame.build_id = "deadbeef";
    frame.file_offset = "0x1234000";
    frame.load_base = "0x2000";
    frame.resolved_build_id = "deadbeef";
    frame.symbol_match_strategy = "build_id";
    frame.function_name = "abort";
    frame.source_file = "/src/libc.cpp";
    frame.source_line = 42;
    frame.source_column = 7;
    result.frames.push_back(frame);

    ndktrace::MemoryMapEntry image;
    image.raw_line = "7b2c400000-7b2c422000 r-xp 00000000 08:01 1234567 /apex/com.android.runtime/lib64/bionic/libc.so";
    image.highlighted = true;
    image.start_address = "0x7b2c400000";
    image.end_address = "0x7b2c422000";
    image.permissions = "r-xp";
    image.image_path = "/apex/com.android.runtime/lib64/bionic/libc.so";
    image.file_offset = "0x00000000";
    image.build_id = "deadbeef";
    image.load_base = "0x7b2c400000";
    result.memory_maps.push_back(image);

    const std::string json = ToJson(result, false);
    Expect(json.find("\"frames\"") != std::string::npos, "Expected frames field in JSON.");
    Expect(json.find("\"artifact\"") != std::string::npos, "Expected artifact field in JSON.");
    Expect(json.find("\"arm64\"") != std::string::npos, "Expected artifact ABI value in JSON.");
    Expect(json.find("\"memoryMaps\"") != std::string::npos, "Expected memoryMaps field in JSON.");
    Expect(json.find("\"symbolHint\"") != std::string::npos, "Expected symbolHint field in JSON.");
    Expect(json.find("\"abort+164\"") != std::string::npos, "Expected symbolHint value in JSON.");
    Expect(json.find("\"buildId\"") != std::string::npos, "Expected buildId field in JSON.");
    Expect(json.find("\"deadbeef\"") != std::string::npos, "Expected buildId value in JSON.");
    Expect(json.find("\"fileOffset\"") != std::string::npos, "Expected fileOffset field in JSON.");
    Expect(json.find("\"0x1234000\"") != std::string::npos, "Expected fileOffset value in JSON.");
    Expect(json.find("\"loadBase\"") != std::string::npos, "Expected loadBase field in JSON.");
    Expect(json.find("\"0x2000\"") != std::string::npos, "Expected loadBase value in JSON.");
    Expect(json.find("\"resolvedBuildId\"") != std::string::npos, "Expected resolvedBuildId field in JSON.");
    Expect(json.find("\"symbolMatchStrategy\"") != std::string::npos, "Expected symbolMatchStrategy field in JSON.");
    Expect(json.find("\"build_id\"") != std::string::npos, "Expected build_id strategy value in JSON.");
    Expect(json.find("\"column\"") != std::string::npos, "Expected source column field in JSON.");
    Expect(json.find("\"line\": 42") != std::string::npos, "Expected source line value in JSON.");
    Expect(json.find("\"frameKind\"") != std::string::npos, "Expected frameKind field in JSON.");
    Expect(json.find("\"shared_object\"") != std::string::npos, "Expected frameKind value in JSON.");
    Expect(json.find("\"frameSuffix\"") != std::string::npos, "Expected frameSuffix field in JSON.");
    Expect(json.find("\"(abort+164)\"") != std::string::npos, "Expected frameSuffix value in JSON.");
    Expect(json.find("\"toolchain\"") != std::string::npos, "Expected toolchain field in JSON.");
}

}  // namespace

int main() {
    TestTombstoneFrameWithHint();
    TestBacktraceFrameWithoutPcToken();
    TestCompactFrame();
    TestApkFrameClassification();
    TestDexFrameClassification();
    TestLoadBaseExtraction();
    TestSharedLibraryFixture();
    TestApkFixture();
    TestDexFixture();
    TestMemoryMapFixture();
    TestArtifactHeaderParsing();
    TestUnmatchedLine();
    TestSymbolizerOutputParsing();
    TestWindowsSymbolizerOutputParsing();
    TestAddr2lineOutputParsing();
    TestJsonContract();
    return 0;
}

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace ndktrace {

struct ProcessResult {
    int exit_code = -1;
    std::string output;
};

struct ToolchainInfo {
    std::string ndk_path;
    std::string symbolizer_path;
    std::string addr2line_path;
    std::string selected_tool;
    bool has_symbolizer = false;
    bool has_addr2line = false;
};

struct RestoreRequest {
    std::string ndk_path;
    std::string so_path;
    std::string stack_file;
    bool read_stdin = false;
    bool recursive_so_search = true;
    bool pretty_json = false;
    std::string tool_preference = "auto";
    std::string match_mode = "basename";
};

struct FrameResult {
    std::size_t index = 0;
    std::string raw_line;
    bool matched = false;
    std::string status = "unmatched";
    int frame_number = -1;
    std::string frame_kind;
    std::string frame_suffix;
    std::string address;
    std::string library_name;
    std::string library_path;
    std::string symbol_hint;
    std::string build_id;
    std::string file_offset;
    std::string load_base;
    std::string resolved_build_id;
    std::string symbol_match_strategy;
    std::string resolved_so_path;
    std::string tool_used;
    std::string symbol_text;
    std::string function_name;
    std::string source_file;
    int source_line = -1;
    int source_column = -1;
    std::vector<std::string> warnings;
};

struct MemoryMapEntry {
    std::string raw_line;
    bool highlighted = false;
    std::string start_address;
    std::string end_address;
    std::string permissions;
    std::string image_path;
    std::string file_offset;
    std::string build_id;
    std::string load_base;
};

struct CrashArtifactInfo {
    std::string abi;
    std::string signal;
    std::string tombstone_path;
    bool has_backtrace = false;
    bool has_memory_map = false;
};

struct RestoreSummary {
    std::size_t frames_total = 0;
    std::size_t frames_matched = 0;
    std::size_t frames_resolved = 0;
    std::size_t frames_unresolved = 0;
    std::size_t frames_missing_so = 0;
    std::size_t frames_tool_failed = 0;
};

struct RestoreResult {
    bool ok = false;
    RestoreRequest request;
    CrashArtifactInfo artifact;
    ToolchainInfo toolchain;
    RestoreSummary summary;
    std::vector<FrameResult> frames;
    std::vector<MemoryMapEntry> memory_maps;
    std::vector<std::string> errors;
};

struct ScanRequest {
    bool pretty_json = false;
};

struct ScanResult {
    bool ok = true;
    std::vector<std::string> candidates;
    std::vector<std::string> warnings;
};

struct ValidateRequest {
    std::string ndk_path;
    std::string so_path;
    bool pretty_json = false;
};

struct ValidateResult {
    bool ok = false;
    std::string ndk_path;
    std::string so_path;
    bool ndk_exists = false;
    bool ndk_valid = false;
    bool so_exists = false;
    bool so_is_file = false;
    bool so_is_directory = false;
    ToolchainInfo toolchain;
    std::vector<std::string> errors;
};

}  // namespace ndktrace

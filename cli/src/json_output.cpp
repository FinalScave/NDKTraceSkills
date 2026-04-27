#include "ndktrace/json_output.h"

#include <sstream>

namespace ndktrace {
namespace {

std::string Escape(const std::string& value) {
    std::ostringstream out;
    for (char ch : value) {
        switch (ch) {
            case '\\':
                out << "\\\\";
                break;
            case '"':
                out << "\\\"";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                out << ch;
                break;
        }
    }
    return out.str();
}

std::string Quote(const std::string& value) {
    return "\"" + Escape(value) + "\"";
}

void NewLine(std::ostringstream& out, bool pretty) {
    if (pretty) {
        out << '\n';
    }
}

void Indent(std::ostringstream& out, bool pretty, int level) {
    if (pretty) {
        out << std::string(level * 2, ' ');
    }
}

void WriteStringArray(
    std::ostringstream& out,
    const std::vector<std::string>& values,
    bool pretty,
    int level) {
    out << "[";
    if (!values.empty()) {
        NewLine(out, pretty);
    }
    for (std::size_t i = 0; i < values.size(); ++i) {
        Indent(out, pretty, level + 1);
        out << Quote(values[i]);
        if (i + 1 != values.size()) {
            out << ",";
        }
        NewLine(out, pretty);
    }
    if (!values.empty()) {
        Indent(out, pretty, level);
    }
    out << "]";
}

void WriteToolchain(std::ostringstream& out, const ToolchainInfo& toolchain, bool pretty, int level) {
    out << "{";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"ndkPath\": " << Quote(toolchain.ndk_path) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"symbolizer\": " << Quote(toolchain.symbolizer_path) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"addr2line\": " << Quote(toolchain.addr2line_path) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"selectedTool\": " << Quote(toolchain.selected_tool) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"hasSymbolizer\": " << (toolchain.has_symbolizer ? "true" : "false") << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"hasAddr2line\": " << (toolchain.has_addr2line ? "true" : "false");
    NewLine(out, pretty);
    Indent(out, pretty, level);
    out << "}";
}

void WriteModuleCandidate(
    std::ostringstream& out,
    const ProjectModuleCandidate& candidate,
    bool pretty,
    int level) {
    out << "{";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"name\": " << Quote(candidate.name) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"path\": " << Quote(candidate.path) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"hasNativeBuild\": " << (candidate.has_native_build ? "true" : "false") << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"ndkVersion\": " << Quote(candidate.ndk_version) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"ndkPath\": " << Quote(candidate.ndk_path);
    NewLine(out, pretty);
    Indent(out, pretty, level);
    out << "}";
}

void WriteSymbolCandidate(
    std::ostringstream& out,
    const ProjectSymbolCandidate& candidate,
    bool pretty,
    int level) {
    out << "{";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"path\": " << Quote(candidate.path) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"libraryName\": " << Quote(candidate.library_name) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"source\": " << Quote(candidate.source) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"module\": " << Quote(candidate.module) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"variant\": " << Quote(candidate.variant) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"abi\": " << Quote(candidate.abi) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"stripped\": " << (candidate.stripped ? "true" : "false") << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"score\": " << candidate.score;
    NewLine(out, pretty);
    Indent(out, pretty, level);
    out << "}";
}

template <typename T, typename Writer>
void WriteObjectArray(
    std::ostringstream& out,
    const std::vector<T>& values,
    bool pretty,
    int level,
    Writer writer) {
    out << "[";
    if (!values.empty()) {
        NewLine(out, pretty);
    }
    for (std::size_t i = 0; i < values.size(); ++i) {
        Indent(out, pretty, level + 1);
        writer(out, values[i], pretty, level + 1);
        if (i + 1 != values.size()) {
            out << ",";
        }
        NewLine(out, pretty);
    }
    if (!values.empty()) {
        Indent(out, pretty, level);
    }
    out << "]";
}

void WriteProjectResolution(
    std::ostringstream& out,
    const ProjectResolutionContext& context,
    bool pretty,
    int level) {
    out << "{";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"attempted\": " << (context.attempted ? "true" : "false") << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"ok\": " << (context.ok ? "true" : "false") << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"status\": " << Quote(context.status) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"inputPath\": " << Quote(context.input_path) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"projectPath\": " << Quote(context.project_path) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"module\": " << Quote(context.module) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"moduleName\": " << Quote(context.module_name) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"modulePath\": " << Quote(context.module_path) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"variant\": " << Quote(context.variant) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"abi\": " << Quote(context.abi) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"sdkDir\": " << Quote(context.sdk_dir) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"ndkVersion\": " << Quote(context.ndk_version) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"ndkPath\": " << Quote(context.ndk_path) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"moduleCandidates\": ";
    WriteObjectArray(out, context.module_candidates, pretty, level + 1, WriteModuleCandidate);
    out << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"libraryNames\": ";
    WriteStringArray(out, context.library_names, pretty, level + 1);
    out << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"preferredSymbolPath\": " << Quote(context.preferred_symbol_path) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"symbolCandidates\": ";
    WriteObjectArray(out, context.symbol_candidates, pretty, level + 1, WriteSymbolCandidate);
    out << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"warnings\": ";
    WriteStringArray(out, context.warnings, pretty, level + 1);
    out << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"ambiguities\": ";
    WriteStringArray(out, context.ambiguities, pretty, level + 1);
    out << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"errors\": ";
    WriteStringArray(out, context.errors, pretty, level + 1);
    NewLine(out, pretty);
    Indent(out, pretty, level);
    out << "}";
}

void WriteArtifact(std::ostringstream& out, const CrashArtifactInfo& artifact, bool pretty, int level) {
    out << "{";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"abi\": " << Quote(artifact.abi) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"signal\": " << Quote(artifact.signal) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"tombstonePath\": " << Quote(artifact.tombstone_path) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"hasBacktrace\": " << (artifact.has_backtrace ? "true" : "false") << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"hasMemoryMap\": " << (artifact.has_memory_map ? "true" : "false");
    NewLine(out, pretty);
    Indent(out, pretty, level);
    out << "}";
}

void WriteMemoryMapEntry(std::ostringstream& out, const MemoryMapEntry& entry, bool pretty, int level) {
    out << "{";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"rawLine\": " << Quote(entry.raw_line) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"highlighted\": " << (entry.highlighted ? "true" : "false") << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"startAddress\": " << Quote(entry.start_address) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"endAddress\": " << Quote(entry.end_address) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"permissions\": " << Quote(entry.permissions) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"imagePath\": " << Quote(entry.image_path) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"fileOffset\": " << Quote(entry.file_offset) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"buildId\": " << Quote(entry.build_id) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"loadBase\": " << Quote(entry.load_base);
    NewLine(out, pretty);
    Indent(out, pretty, level);
    out << "}";
}

void WriteFrame(std::ostringstream& out, const FrameResult& frame, bool pretty, int level) {
    out << "{";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"index\": " << frame.index << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"rawLine\": " << Quote(frame.raw_line) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"matched\": " << (frame.matched ? "true" : "false") << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"status\": " << Quote(frame.status) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"frameNumber\": " << frame.frame_number << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"frameKind\": " << Quote(frame.frame_kind) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"frameSuffix\": " << Quote(frame.frame_suffix) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"address\": " << Quote(frame.address) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"libraryName\": " << Quote(frame.library_name) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"libraryPath\": " << Quote(frame.library_path) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"symbolHint\": " << Quote(frame.symbol_hint) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"buildId\": " << Quote(frame.build_id) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"fileOffset\": " << Quote(frame.file_offset) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"loadBase\": " << Quote(frame.load_base) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"resolvedBuildId\": " << Quote(frame.resolved_build_id) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"symbolMatchStrategy\": " << Quote(frame.symbol_match_strategy) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"resolvedSoPath\": " << Quote(frame.resolved_so_path) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"toolUsed\": " << Quote(frame.tool_used) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"symbol\": {";
    NewLine(out, pretty);
    Indent(out, pretty, level + 2);
    out << "\"text\": " << Quote(frame.symbol_text) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 2);
    out << "\"function\": " << Quote(frame.function_name) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 2);
    out << "\"file\": " << Quote(frame.source_file) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 2);
    out << "\"line\": " << frame.source_line << ",";
    NewLine(out, pretty);
    Indent(out, pretty, level + 2);
    out << "\"column\": " << frame.source_column;
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "},";
    NewLine(out, pretty);
    Indent(out, pretty, level + 1);
    out << "\"warnings\": ";
    WriteStringArray(out, frame.warnings, pretty, level + 1);
    NewLine(out, pretty);
    Indent(out, pretty, level);
    out << "}";
}

}  // namespace

std::string ToJson(const ResolveProjectResult& result, bool pretty) {
    std::ostringstream out;
    out << "{";
    NewLine(out, pretty);

    Indent(out, pretty, 1);
    out << "\"meta\": {";
    NewLine(out, pretty);
    Indent(out, pretty, 2);
    out << "\"tool\": \"ndktrace-cli\",";
    NewLine(out, pretty);
    Indent(out, pretty, 2);
    out << "\"version\": \"0.1.0\",";
    NewLine(out, pretty);
    Indent(out, pretty, 2);
    out << "\"ok\": " << (result.ok ? "true" : "false");
    NewLine(out, pretty);
    Indent(out, pretty, 1);
    out << "},";
    NewLine(out, pretty);

    Indent(out, pretty, 1);
    out << "\"request\": {";
    NewLine(out, pretty);
    Indent(out, pretty, 2);
    out << "\"projectPath\": " << Quote(result.request.project_path) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, 2);
    out << "\"module\": " << Quote(result.request.module_name) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, 2);
    out << "\"variant\": " << Quote(result.request.variant) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, 2);
    out << "\"abi\": " << Quote(result.request.abi) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, 2);
    out << "\"library\": " << Quote(result.request.library_name) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, 2);
    out << "\"stackSource\": " << Quote(result.request.read_stdin ? "stdin" : result.request.stack_file);
    NewLine(out, pretty);
    Indent(out, pretty, 1);
    out << "},";
    NewLine(out, pretty);

    Indent(out, pretty, 1);
    out << "\"projectResolution\": ";
    WriteProjectResolution(out, result.project_resolution, pretty, 1);
    out << ",";
    NewLine(out, pretty);

    Indent(out, pretty, 1);
    out << "\"errors\": ";
    WriteStringArray(out, result.errors, pretty, 1);
    NewLine(out, pretty);
    out << "}";
    return out.str();
}

std::string ToJson(const RestoreResult& result, bool pretty) {
    std::ostringstream out;
    out << "{";
    NewLine(out, pretty);

    Indent(out, pretty, 1);
    out << "\"meta\": {";
    NewLine(out, pretty);
    Indent(out, pretty, 2);
    out << "\"tool\": \"ndktrace-cli\",";
    NewLine(out, pretty);
    Indent(out, pretty, 2);
    out << "\"version\": \"0.1.0\",";
    NewLine(out, pretty);
    Indent(out, pretty, 2);
    out << "\"ok\": " << (result.ok ? "true" : "false");
    NewLine(out, pretty);
    Indent(out, pretty, 1);
    out << "},";
    NewLine(out, pretty);

    Indent(out, pretty, 1);
    out << "\"request\": {";
    NewLine(out, pretty);
    Indent(out, pretty, 2);
    out << "\"ndkPath\": " << Quote(result.request.ndk_path) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, 2);
    out << "\"soPath\": " << Quote(result.request.so_path) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, 2);
    out << "\"projectPath\": " << Quote(result.request.project_path) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, 2);
    out << "\"module\": " << Quote(result.request.module_name) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, 2);
    out << "\"variant\": " << Quote(result.request.variant) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, 2);
    out << "\"abi\": " << Quote(result.request.abi) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, 2);
    out << "\"library\": " << Quote(result.request.library_name) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, 2);
    out << "\"stackSource\": " << Quote(result.request.read_stdin ? "stdin" : result.request.stack_file) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, 2);
    out << "\"toolPreference\": " << Quote(result.request.tool_preference) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, 2);
    out << "\"matchMode\": " << Quote(result.request.match_mode) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, 2);
    out << "\"recursiveSoSearch\": " << (result.request.recursive_so_search ? "true" : "false");
    NewLine(out, pretty);
    Indent(out, pretty, 1);
    out << "},";
    NewLine(out, pretty);

    Indent(out, pretty, 1);
    out << "\"artifact\": ";
    WriteArtifact(out, result.artifact, pretty, 1);
    out << ",";
    NewLine(out, pretty);

    Indent(out, pretty, 1);
    out << "\"projectResolution\": ";
    WriteProjectResolution(out, result.project_resolution, pretty, 1);
    out << ",";
    NewLine(out, pretty);

    Indent(out, pretty, 1);
    out << "\"toolchain\": ";
    WriteToolchain(out, result.toolchain, pretty, 1);
    out << ",";
    NewLine(out, pretty);

    Indent(out, pretty, 1);
    out << "\"summary\": {";
    NewLine(out, pretty);
    Indent(out, pretty, 2);
    out << "\"framesTotal\": " << result.summary.frames_total << ",";
    NewLine(out, pretty);
    Indent(out, pretty, 2);
    out << "\"framesMatched\": " << result.summary.frames_matched << ",";
    NewLine(out, pretty);
    Indent(out, pretty, 2);
    out << "\"framesResolved\": " << result.summary.frames_resolved << ",";
    NewLine(out, pretty);
    Indent(out, pretty, 2);
    out << "\"framesUnresolved\": " << result.summary.frames_unresolved << ",";
    NewLine(out, pretty);
    Indent(out, pretty, 2);
    out << "\"framesMissingSo\": " << result.summary.frames_missing_so << ",";
    NewLine(out, pretty);
    Indent(out, pretty, 2);
    out << "\"framesToolFailed\": " << result.summary.frames_tool_failed;
    NewLine(out, pretty);
    Indent(out, pretty, 1);
    out << "},";
    NewLine(out, pretty);

    Indent(out, pretty, 1);
    out << "\"frames\": [";
    if (!result.frames.empty()) {
        NewLine(out, pretty);
    }
    for (std::size_t i = 0; i < result.frames.size(); ++i) {
        Indent(out, pretty, 2);
        WriteFrame(out, result.frames[i], pretty, 2);
        if (i + 1 != result.frames.size()) {
            out << ",";
        }
        NewLine(out, pretty);
    }
    if (!result.frames.empty()) {
        Indent(out, pretty, 1);
    }
    out << "],";
    NewLine(out, pretty);

    Indent(out, pretty, 1);
    out << "\"memoryMaps\": [";
    if (!result.memory_maps.empty()) {
        NewLine(out, pretty);
    }
    for (std::size_t i = 0; i < result.memory_maps.size(); ++i) {
        Indent(out, pretty, 2);
        WriteMemoryMapEntry(out, result.memory_maps[i], pretty, 2);
        if (i + 1 != result.memory_maps.size()) {
            out << ",";
        }
        NewLine(out, pretty);
    }
    if (!result.memory_maps.empty()) {
        Indent(out, pretty, 1);
    }
    out << "],";
    NewLine(out, pretty);

    Indent(out, pretty, 1);
    out << "\"errors\": ";
    WriteStringArray(out, result.errors, pretty, 1);
    NewLine(out, pretty);
    out << "}";
    return out.str();
}

std::string ToJson(const ScanResult& result, bool pretty) {
    std::ostringstream out;
    out << "{";
    NewLine(out, pretty);
    Indent(out, pretty, 1);
    out << "\"ok\": " << (result.ok ? "true" : "false") << ",";
    NewLine(out, pretty);
    Indent(out, pretty, 1);
    out << "\"candidates\": ";
    WriteStringArray(out, result.candidates, pretty, 1);
    out << ",";
    NewLine(out, pretty);
    Indent(out, pretty, 1);
    out << "\"warnings\": ";
    WriteStringArray(out, result.warnings, pretty, 1);
    NewLine(out, pretty);
    out << "}";
    return out.str();
}

std::string ToJson(const ValidateResult& result, bool pretty) {
    std::ostringstream out;
    out << "{";
    NewLine(out, pretty);
    Indent(out, pretty, 1);
    out << "\"ok\": " << (result.ok ? "true" : "false") << ",";
    NewLine(out, pretty);
    Indent(out, pretty, 1);
    out << "\"ndkPath\": " << Quote(result.ndk_path) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, 1);
    out << "\"soPath\": " << Quote(result.so_path) << ",";
    NewLine(out, pretty);
    Indent(out, pretty, 1);
    out << "\"projectResolution\": ";
    WriteProjectResolution(out, result.project_resolution, pretty, 1);
    out << ",";
    NewLine(out, pretty);
    Indent(out, pretty, 1);
    out << "\"ndkExists\": " << (result.ndk_exists ? "true" : "false") << ",";
    NewLine(out, pretty);
    Indent(out, pretty, 1);
    out << "\"ndkValid\": " << (result.ndk_valid ? "true" : "false") << ",";
    NewLine(out, pretty);
    Indent(out, pretty, 1);
    out << "\"soExists\": " << (result.so_exists ? "true" : "false") << ",";
    NewLine(out, pretty);
    Indent(out, pretty, 1);
    out << "\"soIsFile\": " << (result.so_is_file ? "true" : "false") << ",";
    NewLine(out, pretty);
    Indent(out, pretty, 1);
    out << "\"soIsDirectory\": " << (result.so_is_directory ? "true" : "false") << ",";
    NewLine(out, pretty);
    Indent(out, pretty, 1);
    out << "\"toolchain\": ";
    WriteToolchain(out, result.toolchain, pretty, 1);
    out << ",";
    NewLine(out, pretty);
    Indent(out, pretty, 1);
    out << "\"errors\": ";
    WriteStringArray(out, result.errors, pretty, 1);
    NewLine(out, pretty);
    out << "}";
    return out.str();
}

}  // namespace ndktrace

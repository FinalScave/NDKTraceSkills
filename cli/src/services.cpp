#include "ndktrace/services.h"

#include <cctype>
#include <filesystem>

#include "ndktrace/crash_artifact_parser.h"
#include "ndktrace/parser.h"
#include "ndktrace/platform.h"
#include "ndktrace/project_resolver.h"
#include "ndktrace/symbol_file_resolver.h"

namespace ndktrace {
namespace {

bool EndsWithNoCase(const std::string& value, const std::string& suffix) {
    if (value.size() < suffix.size()) {
        return false;
    }
    const std::size_t offset = value.size() - suffix.size();
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(value[offset + i])) !=
            std::tolower(static_cast<unsigned char>(suffix[i]))) {
            return false;
        }
    }
    return true;
}

const MemoryMapEntry* FindMemoryMapForFrame(
    const std::vector<MemoryMapEntry>& memory_maps,
    const FrameResult& frame) {
    const MemoryMapEntry* exact_match = nullptr;
    const MemoryMapEntry* basename_match = nullptr;

    for (const auto& entry : memory_maps) {
        if (entry.image_path == frame.library_path) {
            exact_match = &entry;
            break;
        }
        if (basename_match == nullptr && EndsWithNoCase(entry.image_path, frame.library_name)) {
            basename_match = &entry;
        }
    }

    return exact_match != nullptr ? exact_match : basename_match;
}

void ApplyMemoryMapMetadata(const MemoryMapEntry* image, FrameResult& frame) {
    if (image == nullptr) {
        return;
    }

    if (frame.build_id.empty()) {
        frame.build_id = image->build_id;
    }
    if (frame.file_offset.empty()) {
        frame.file_offset = image->file_offset;
    }
    if (frame.load_base.empty()) {
        frame.load_base = image->load_base;
    }
}

std::vector<std::string> BuildToolCommand(
    const ToolchainInfo& toolchain,
    const std::filesystem::path& so_file,
    const std::string& address,
    const std::string& preference,
    std::string& selected_tool) {
    if ((preference == "auto" || preference == "symbolizer") && toolchain.has_symbolizer) {
        selected_tool = "llvm-symbolizer";
        return {toolchain.symbolizer_path, "-e", so_file.string(), NormalizeAddress(address)};
    }

    if ((preference == "auto" || preference == "addr2line") && toolchain.has_addr2line) {
        selected_tool = "llvm-addr2line";
        return {toolchain.addr2line_path, "-e", so_file.string(), "-f", "-C", "-p", NormalizeAddress(address)};
    }

    selected_tool.clear();
    return {};
}

std::string ReadOptionalStackInput(const std::string& stack_file, bool read_stdin) {
    if (read_stdin) {
        return ReadStdIn();
    }
    if (!stack_file.empty()) {
        return ReadTextFile(stack_file);
    }
    return {};
}

ProjectResolverRequest BuildProjectRequest(
    const std::string& project_path,
    const std::string& module_name,
    const std::string& variant,
    const std::string& abi,
    const std::string& library_name) {
    ProjectResolverRequest project_request;
    project_request.project_path = project_path;
    project_request.module_name = module_name;
    project_request.variant = variant;
    project_request.abi = abi;
    project_request.library_name = library_name;
    return project_request;
}

bool ProjectResolutionCanSupplyPaths(const ProjectResolutionContext& context) {
    return context.attempted && context.status == "resolved";
}

void AppendProjectResolutionErrors(
    const ProjectResolutionContext& context,
    std::vector<std::string>& errors) {
    if (!context.attempted || context.status == "resolved") {
        return;
    }

    if (!context.errors.empty()) {
        errors.insert(errors.end(), context.errors.begin(), context.errors.end());
    }
    if (!context.ambiguities.empty()) {
        errors.insert(errors.end(), context.ambiguities.begin(), context.ambiguities.end());
    }
    if (errors.empty()) {
        errors.push_back("Android project inputs could not be fully resolved.");
    }
}

}  // namespace

ResolveProjectResult RunResolveProject(const ResolveProjectRequest& request) {
    ResolveProjectResult result;
    result.request = request;

    if (request.project_path.empty()) {
        result.errors.push_back("The --project argument is required.");
        return result;
    }

    const std::string crash_text = ReadOptionalStackInput(request.stack_file, request.read_stdin);
    result.project_resolution = ResolveAndroidProject(
        BuildProjectRequest(
            request.project_path,
            request.module_name,
            request.variant,
            request.abi,
            request.library_name),
        crash_text);
    result.ok = result.project_resolution.ok;
    result.errors = result.project_resolution.errors;
    if (!result.project_resolution.ok && result.errors.empty() && !result.project_resolution.ambiguities.empty()) {
        result.errors = result.project_resolution.ambiguities;
    }
    return result;
}

RestoreResult RunRestore(const RestoreRequest& request) {
    RestoreResult result;
    RestoreRequest effective_request = request;
    result.request = effective_request;

    if (!effective_request.read_stdin && effective_request.stack_file.empty()) {
        result.errors.push_back("Use --stack-file or --stdin to provide a stack trace.");
    }
    if (!result.errors.empty()) {
        return result;
    }

    const std::string stack_input = ReadOptionalStackInput(effective_request.stack_file, effective_request.read_stdin);

    if ((!effective_request.ndk_path.empty() || !effective_request.so_path.empty()) == false &&
        effective_request.project_path.empty()) {
        result.errors.push_back("Provide --ndk and --so, or use --project to resolve them from an Android project.");
        return result;
    }

    if (!effective_request.project_path.empty() &&
        (effective_request.ndk_path.empty() || effective_request.so_path.empty())) {
        result.project_resolution = ResolveAndroidProject(
            BuildProjectRequest(
                effective_request.project_path,
                effective_request.module_name,
                effective_request.variant,
                effective_request.abi,
                effective_request.library_name),
            stack_input);
        if (effective_request.ndk_path.empty() && ProjectResolutionCanSupplyPaths(result.project_resolution)) {
            effective_request.ndk_path = result.project_resolution.ndk_path;
        }
        if (effective_request.so_path.empty() && ProjectResolutionCanSupplyPaths(result.project_resolution)) {
            effective_request.so_path = result.project_resolution.preferred_symbol_path;
        }
        AppendProjectResolutionErrors(result.project_resolution, result.errors);
    }

    if (effective_request.ndk_path.empty()) {
        result.errors.push_back("The --ndk argument is required.");
    }
    if (effective_request.so_path.empty()) {
        result.errors.push_back("The --so argument is required.");
    }
    if (!result.errors.empty()) {
        result.request = effective_request;
        return result;
    }

    result.request = effective_request;

    const std::filesystem::path ndk_path(effective_request.ndk_path);
    const std::filesystem::path so_path(effective_request.so_path);
    if (!IsValidNdkDirectory(ndk_path)) {
        result.errors.push_back("The NDK path does not look valid: " + effective_request.ndk_path);
        return result;
    }
    if (!std::filesystem::exists(so_path)) {
        result.errors.push_back("The symbol path does not exist: " + effective_request.so_path);
        return result;
    }

    result.toolchain = ResolveToolchain(ndk_path);
    if (!result.toolchain.has_symbolizer && !result.toolchain.has_addr2line) {
        result.errors.push_back("No llvm-symbolizer or llvm-addr2line binary was found under the NDK.");
        return result;
    }

    const ParsedCrashArtifact artifact = ParseCrashArtifactText(stack_input);
    result.artifact = artifact.artifact;
    result.memory_maps = artifact.memory_maps;

    result.summary.frames_total = artifact.frame_lines.size();

    for (const auto& candidate : artifact.frame_lines) {
        FrameResult frame;
        frame.index = candidate.index;
        frame.raw_line = candidate.text;

        const ParsedFrame parsed = ParseFrameLine(candidate.text);
        frame.matched = parsed.matched;
        if (!parsed.matched) {
            frame.status = "unmatched";
            ++result.summary.frames_unresolved;
            result.frames.push_back(frame);
            continue;
        }

        ++result.summary.frames_matched;
        frame.frame_number = parsed.frame_number;
        frame.frame_kind = parsed.frame_kind;
        frame.frame_suffix = parsed.frame_suffix;
        frame.address = NormalizeAddress(parsed.address);
        frame.library_name = parsed.library_name;
        frame.library_path = parsed.library_path;
        frame.symbol_hint = parsed.symbol_hint;
        frame.build_id = parsed.build_id;
        frame.file_offset = parsed.file_offset;
        frame.load_base = parsed.load_base;
        ApplyMemoryMapMetadata(FindMemoryMapForFrame(result.memory_maps, frame), frame);

        const SymbolFileLookupResult symbol_file = ResolveSymbolFile({
            so_path,
            parsed.library_name,
            parsed.library_path,
            frame.build_id,
            effective_request.recursive_so_search,
            effective_request.match_mode});
        if (!symbol_file.path.has_value()) {
            frame.status = symbol_file.had_build_id_mismatch ? "build_id_mismatch" : "missing_so";
            if (symbol_file.had_build_id_mismatch && !frame.build_id.empty()) {
                frame.warnings.push_back(
                    "Candidate symbol files were found, but their BuildId did not match the crash artifact.");
            } else {
                frame.warnings.push_back("No matching local symbol file was found.");
            }
            ++result.summary.frames_missing_so;
            ++result.summary.frames_unresolved;
            result.frames.push_back(frame);
            continue;
        }

        frame.resolved_build_id = symbol_file.file_build_id;
        frame.symbol_match_strategy = symbol_file.strategy;
        frame.resolved_so_path = ToGenericString(*symbol_file.path);
        if (!frame.build_id.empty() && frame.symbol_match_strategy != "build_id") {
            frame.warnings.push_back(
                "The selected local symbol file was chosen without a verified BuildId match.");
        }

        std::string selected_tool;
        const auto command = BuildToolCommand(
            result.toolchain,
            *symbol_file.path,
            parsed.address,
            effective_request.tool_preference,
            selected_tool);
        if (command.empty()) {
            frame.status = "tool_missing";
            frame.warnings.push_back("The requested symbolization tool is not available.");
            ++result.summary.frames_tool_failed;
            ++result.summary.frames_unresolved;
            result.frames.push_back(frame);
            continue;
        }

        frame.tool_used = selected_tool;
        result.toolchain.selected_tool = selected_tool;
        const ProcessResult tool_result = RunProcess(command);
        if (tool_result.exit_code != 0 || tool_result.output.empty()) {
            frame.status = "tool_failed";
            frame.warnings.push_back(tool_result.output.empty() ? "The symbolization tool did not produce output." : tool_result.output);
            ++result.summary.frames_tool_failed;
            ++result.summary.frames_unresolved;
            result.frames.push_back(frame);
            continue;
        }

        ApplyToolOutput(tool_result.output, selected_tool, frame);
        frame.status = "resolved";
        ++result.summary.frames_resolved;
        result.frames.push_back(frame);
    }

    result.ok = result.errors.empty();
    return result;
}

ScanResult RunScan(const ScanRequest&) {
    ScanResult result;
    for (const auto& candidate : DiscoverNdkCandidates()) {
        result.candidates.push_back(ToGenericString(candidate));
    }
    if (result.candidates.empty()) {
        result.warnings.push_back("No local Android NDK installation was detected.");
    }
    return result;
}

ValidateResult RunValidate(const ValidateRequest& request) {
    ValidateResult result;
    ValidateRequest effective_request = request;

    if ((!effective_request.ndk_path.empty() || !effective_request.so_path.empty()) == false &&
        effective_request.project_path.empty()) {
        result.errors.push_back("Provide --ndk and --so, or use --project to resolve them from an Android project.");
    }

    if (!effective_request.project_path.empty() &&
        (effective_request.ndk_path.empty() || effective_request.so_path.empty())) {
        const std::string crash_text = ReadOptionalStackInput(effective_request.stack_file, effective_request.read_stdin);
        result.project_resolution = ResolveAndroidProject(
            BuildProjectRequest(
                effective_request.project_path,
                effective_request.module_name,
                effective_request.variant,
                effective_request.abi,
                effective_request.library_name),
            crash_text);
        if (effective_request.ndk_path.empty() && ProjectResolutionCanSupplyPaths(result.project_resolution)) {
            effective_request.ndk_path = result.project_resolution.ndk_path;
        }
        if (effective_request.so_path.empty() && ProjectResolutionCanSupplyPaths(result.project_resolution)) {
            effective_request.so_path = result.project_resolution.preferred_symbol_path;
        }
        AppendProjectResolutionErrors(result.project_resolution, result.errors);
    }

    result.ndk_path = effective_request.ndk_path;
    result.so_path = effective_request.so_path;

    if (effective_request.ndk_path.empty()) {
        result.errors.push_back("The --ndk argument is required.");
    }
    if (effective_request.so_path.empty()) {
        result.errors.push_back("The --so argument is required.");
    }
    if (!result.errors.empty()) {
        return result;
    }

    const std::filesystem::path ndk_path(effective_request.ndk_path);
    const std::filesystem::path so_path(effective_request.so_path);

    result.ndk_exists = std::filesystem::exists(ndk_path);
    result.ndk_valid = IsValidNdkDirectory(ndk_path);
    result.so_exists = std::filesystem::exists(so_path);
    result.so_is_file = result.so_exists && std::filesystem::is_regular_file(so_path);
    result.so_is_directory = result.so_exists && std::filesystem::is_directory(so_path);
    result.toolchain = ResolveToolchain(ndk_path);

    if (!result.ndk_exists) {
        result.errors.push_back("The NDK path does not exist.");
    } else if (!result.ndk_valid) {
        result.errors.push_back("The NDK path exists but does not look valid.");
    }
    if (!result.so_exists) {
        result.errors.push_back("The symbol path does not exist.");
    }
    if (!result.so_is_file && !result.so_is_directory) {
        result.errors.push_back("The symbol path must point to a file or a directory.");
    }
    if (!result.toolchain.has_symbolizer && !result.toolchain.has_addr2line) {
        result.errors.push_back("No symbolization tool was found under the NDK.");
    }

    result.ok = result.errors.empty();
    return result;
}

}  // namespace ndktrace

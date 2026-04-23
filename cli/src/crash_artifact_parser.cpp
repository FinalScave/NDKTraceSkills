#include "ndktrace/crash_artifact_parser.h"

#include <cctype>
#include <regex>
#include <string>
#include <vector>

#include "ndktrace/parser.h"

namespace ndktrace {
namespace {

bool StartsWithNoCase(const std::string& value, const std::string& prefix) {
    if (value.size() < prefix.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(value[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

MemoryMapEntry ToMemoryMapEntry(const ParsedImageMetadata& image) {
    MemoryMapEntry entry;
    entry.raw_line = image.raw_line;
    entry.highlighted = image.highlighted;
    entry.start_address = image.start_address;
    entry.end_address = image.end_address;
    entry.permissions = image.permissions;
    entry.image_path = image.image_path;
    entry.file_offset = image.file_offset;
    entry.build_id = image.build_id;
    entry.load_base = image.load_base;
    return entry;
}

std::string ExtractEmbeddedArtifactPayload(const std::string& line) {
    static const std::vector<std::regex> patterns = {
        std::regex(
            R"((#\d+\s+(?:(?:pc|ip)\s+[0-9A-Fa-f]+|0x[0-9A-Fa-f]+)\s+\S+.*))",
            std::regex_constants::icase),
        std::regex(R"((ABI:\s*'[^']+'.*))", std::regex_constants::icase),
        std::regex(R"((Fatal signal\s+\d+.*))", std::regex_constants::icase),
        std::regex(R"((signal\s+\d+.*))", std::regex_constants::icase),
        std::regex(R"((Tombstone written to:\s*.+))", std::regex_constants::icase),
        std::regex(R"((backtrace:\s*))", std::regex_constants::icase),
        std::regex(R"((memory map:\s*))", std::regex_constants::icase)};

    std::smatch match;
    for (const auto& pattern : patterns) {
        if (std::regex_search(line, match, pattern) && match.size() > 1) {
            return Trim(match[1].str());
        }
    }
    return {};
}

}  // namespace

ParsedCrashArtifact ParseCrashArtifactText(const std::string& text) {
    ParsedCrashArtifact artifact;
    const std::vector<std::string> lines = SplitLines(text);

    std::vector<IndexedLine> backtrace_lines;
    std::vector<IndexedLine> fallback_lines;
    bool in_backtrace = false;
    bool in_memory_map = false;
    bool saw_backtrace_header = false;

    for (std::size_t index = 0; index < lines.size(); ++index) {
        const std::string& line = lines[index];
        const std::string trimmed = Trim(line);
        const std::string extracted = ExtractEmbeddedArtifactPayload(trimmed);
        const std::string normalized = extracted.empty() ? trimmed : extracted;
        if (normalized.empty()) {
            if (in_memory_map) {
                in_memory_map = false;
            }
            if (in_backtrace) {
                in_backtrace = false;
            }
            continue;
        }

        if (const std::string abi = ExtractAbiValue(normalized); !abi.empty()) {
            artifact.artifact.abi = abi;
            continue;
        }
        if (const std::string signal = ExtractSignalSummary(normalized); !signal.empty()) {
            artifact.artifact.signal = signal;
            continue;
        }
        if (const std::string tombstone_path = ExtractTombstonePath(normalized); !tombstone_path.empty()) {
            artifact.artifact.tombstone_path = tombstone_path;
            continue;
        }
        if (StartsWithNoCase(normalized, "backtrace:")) {
            saw_backtrace_header = true;
            artifact.artifact.has_backtrace = true;
            in_backtrace = true;
            in_memory_map = false;
            continue;
        }
        if (StartsWithNoCase(normalized, "memory map:")) {
            artifact.artifact.has_memory_map = true;
            in_memory_map = true;
            in_backtrace = false;
            continue;
        }

        if (in_memory_map) {
            ParsedImageMetadata image = ParseMemoryMapLine(normalized);
            if (image.matched) {
                artifact.memory_maps.push_back(ToMemoryMapEntry(image));
                continue;
            }

            if (!artifact.memory_maps.empty()) {
                ParsedImageMetadata metadata_proxy;
                metadata_proxy.matched = true;
                metadata_proxy.build_id = artifact.memory_maps.back().build_id;
                metadata_proxy.load_base = artifact.memory_maps.back().load_base;
                ApplyMemoryMapMetadataLine(normalized, metadata_proxy);
                artifact.memory_maps.back().build_id = metadata_proxy.build_id;
                artifact.memory_maps.back().load_base = metadata_proxy.load_base;
            }
            continue;
        }

        if (in_backtrace) {
            backtrace_lines.push_back({index, normalized});
            continue;
        }

        if (ParseFrameLine(normalized).matched) {
            fallback_lines.push_back({index, normalized});
        }
    }

    artifact.frame_lines = saw_backtrace_header ? backtrace_lines : fallback_lines;
    return artifact;
}

}  // namespace ndktrace

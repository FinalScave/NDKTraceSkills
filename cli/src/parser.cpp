#include "ndktrace/parser.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <regex>
#include <sstream>

namespace ndktrace {
namespace {

bool StartsWithToken(const std::string& value, std::size_t position, const std::string& token) {
    if (position + token.size() > value.size()) {
        return false;
    }
    for (std::size_t i = 0; i < token.size(); ++i) {
        if (static_cast<char>(std::tolower(static_cast<unsigned char>(value[position + i]))) != token[i]) {
            return false;
        }
    }
    return true;
}

void SkipWhitespace(const std::string& value, std::size_t& position) {
    while (position < value.size() && std::isspace(static_cast<unsigned char>(value[position])) != 0) {
        ++position;
    }
}

std::string ReadToken(const std::string& value, std::size_t& position) {
    const std::size_t start = position;
    while (position < value.size() && std::isspace(static_cast<unsigned char>(value[position])) == 0) {
        ++position;
    }
    return value.substr(start, position - start);
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string ClassifyFrameKind(const std::string& library_path) {
    const std::string lower = ToLower(library_path);
    if (lower.size() >= 3 && lower.substr(lower.size() - 3) == ".so") {
        return "shared_object";
    }
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".apk") {
        return "apk";
    }
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".dex") {
        return "dex";
    }
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".oat") {
        return "oat";
    }
    if (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".odex") {
        return "odex";
    }
    return "unknown";
}

std::string ExtractSymbolHint(const std::string& suffix) {
    std::size_t position = 0;
    while (position < suffix.size()) {
        const auto open = suffix.find('(', position);
        if (open == std::string::npos) {
            break;
        }

        int depth = 1;
        std::size_t close = open + 1;
        for (; close < suffix.size(); ++close) {
            if (suffix[close] == '(') {
                ++depth;
            } else if (suffix[close] == ')') {
                --depth;
                if (depth == 0) {
                    break;
                }
            }
        }
        if (close >= suffix.size() || depth != 0) {
            break;
        }

        const std::string candidate = Trim(suffix.substr(open + 1, close - open - 1));
        const std::string lower = ToLower(candidate);
        if (!candidate.empty() &&
            lower.rfind("buildid:", 0) != 0 &&
            lower.rfind("offset ", 0) != 0 &&
            lower.rfind("load base ", 0) != 0 &&
            lower.rfind("load bias ", 0) != 0) {
            return candidate;
        }

        position = close + 1;
    }
    return {};
}

std::string ExtractTaggedValue(const std::string& suffix, const std::regex& pattern) {
    std::smatch match;
    if (std::regex_search(suffix, match, pattern) && match.size() > 1) {
        return Trim(match[1].str());
    }
    return {};
}

void ParseLocation(const std::string& raw, FrameResult& frame) {
    const std::string trimmed = Trim(raw);
    std::smatch match;

    if (std::regex_match(trimmed, match, std::regex(R"(^(.*):([0-9]+):([0-9]+)$)")) && match.size() > 3) {
        frame.source_file = Trim(match[1].str());
        try {
            frame.source_line = std::stoi(match[2].str());
            frame.source_column = std::stoi(match[3].str());
            return;
        } catch (...) {
            frame.source_file = trimmed;
            frame.source_line = -1;
            frame.source_column = -1;
            return;
        }
    }

    if (std::regex_match(trimmed, match, std::regex(R"(^(.*):([0-9]+)$)")) && match.size() > 2) {
        frame.source_file = Trim(match[1].str());
        try {
            frame.source_line = std::stoi(match[2].str());
            frame.source_column = -1;
            return;
        } catch (...) {
            frame.source_file = trimmed;
            frame.source_line = -1;
            frame.source_column = -1;
            return;
        }
    }

    frame.source_file = trimmed;
    frame.source_line = -1;
    frame.source_column = -1;
}

}  // namespace

std::string Trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        lines.push_back(Trim(line));
    }
    return lines;
}

std::string NormalizeAddress(const std::string& address) {
    if (address.rfind("0x", 0) == 0 || address.rfind("0X", 0) == 0) {
        return address;
    }
    return "0x" + address;
}

std::string ExtractAbiValue(const std::string& line) {
    std::smatch match;
    if (std::regex_search(
            line,
            match,
            std::regex(R"(^\s*ABI:\s*'([^']+)'\s*$)", std::regex_constants::icase)) &&
        match.size() > 1) {
        return Trim(match[1].str());
    }
    return {};
}

std::string ExtractSignalSummary(const std::string& line) {
    const std::string trimmed = Trim(line);
    if (trimmed.rfind("signal ", 0) == 0 || trimmed.rfind("Fatal signal ", 0) == 0) {
        return trimmed;
    }
    return {};
}

std::string ExtractTombstonePath(const std::string& line) {
    std::smatch match;
    if (std::regex_search(
            line,
            match,
            std::regex(R"(^\s*Tombstone written to:\s*(.+?)\s*$)", std::regex_constants::icase)) &&
        match.size() > 1) {
        return Trim(match[1].str());
    }
    return {};
}

ParsedFrame ParseFrameLine(const std::string& line) {
    const std::string trimmed = Trim(line);
    if (trimmed.empty()) {
        return {};
    }

    std::size_t position = 0;
    if (StartsWithToken(trimmed, position, "backtrace:")) {
        position += std::string("backtrace:").size();
        SkipWhitespace(trimmed, position);
    }

    ParsedFrame frame;
    if (position < trimmed.size() && trimmed[position] == '#') {
        ++position;
        const std::size_t number_start = position;
        while (position < trimmed.size() && std::isdigit(static_cast<unsigned char>(trimmed[position])) != 0) {
            ++position;
        }
        if (position > number_start) {
            frame.frame_number = std::stoi(trimmed.substr(number_start, position - number_start));
        }
        SkipWhitespace(trimmed, position);
    }

    if (StartsWithToken(trimmed, position, "pc")) {
        position += 2;
        SkipWhitespace(trimmed, position);
    } else if (StartsWithToken(trimmed, position, "ip")) {
        position += 2;
        SkipWhitespace(trimmed, position);
    }

    const std::string address = ReadToken(trimmed, position);
    if (address.empty()) {
        return {};
    }

    const std::string normalized_address = NormalizeAddress(address);
    if (normalized_address.size() <= 2) {
        return {};
    }
    const bool all_hex = std::all_of(normalized_address.begin() + 2, normalized_address.end(), [](unsigned char ch) {
        return std::isxdigit(ch) != 0;
    });
    if (!all_hex) {
        return {};
    }

    SkipWhitespace(trimmed, position);
    const std::string library_path = ReadToken(trimmed, position);
    if (library_path.empty()) {
        return {};
    }

    frame.matched = true;
    frame.address = address;
    frame.library_path = library_path;
    frame.library_name = std::filesystem::path(frame.library_path).filename().string();
    frame.frame_suffix = Trim(trimmed.substr(position));
    frame.frame_kind = ClassifyFrameKind(frame.library_path);
    frame.symbol_hint = ExtractSymbolHint(frame.frame_suffix);
    frame.build_id = ExtractTaggedValue(
        frame.frame_suffix,
        std::regex(R"(\(\s*BuildId:\s*([^)]+?)\s*\))", std::regex_constants::icase));
    frame.file_offset = ExtractTaggedValue(
        frame.frame_suffix,
        std::regex(R"(\(\s*offset\s+(0x[0-9a-fA-F]+)\s*\))", std::regex_constants::icase));
    frame.load_base = ExtractTaggedValue(
        frame.frame_suffix,
        std::regex(R"(\(\s*load\s+base\s+(0x[0-9a-fA-F]+)\s*\))", std::regex_constants::icase));
    return frame;
}

ParsedImageMetadata ParseMemoryMapLine(const std::string& line) {
    static const std::regex pattern(
        R"(^\s*(--->)?([0-9a-fA-F]+)-([0-9a-fA-F]+)\s+([rwxps-]{3,4})\s+([0-9a-fA-F]+)\s+\S+\s+\S+\s+(.+?)\s*$)",
        std::regex_constants::icase);

    std::smatch match;
    if (!std::regex_match(line, match, pattern)) {
        return {};
    }

    ParsedImageMetadata image;
    image.matched = true;
    image.raw_line = Trim(line);
    image.highlighted = match[1].matched;
    image.start_address = "0x" + match[2].str();
    image.end_address = "0x" + match[3].str();
    image.permissions = match[4].str();
    image.file_offset = "0x" + match[5].str();

    const std::string raw_path_and_tags = Trim(match[6].str());
    const auto tag_start = raw_path_and_tags.find(" (");
    if (tag_start == std::string::npos) {
        image.image_path = raw_path_and_tags;
    } else {
        image.image_path = Trim(raw_path_and_tags.substr(0, tag_start));
    }

    image.build_id = ExtractTaggedValue(
        raw_path_and_tags,
        std::regex(R"(\(\s*BuildId:\s*([^)]+?)\s*\))", std::regex_constants::icase));
    image.load_base = ExtractTaggedValue(
        raw_path_and_tags,
        std::regex(R"(\(\s*load\s+base\s+(0x[0-9a-fA-F]+)\s*\))", std::regex_constants::icase));
    return image;
}

void ApplyMemoryMapMetadataLine(const std::string& line, ParsedImageMetadata& image) {
    if (!image.matched) {
        return;
    }

    if (image.build_id.empty()) {
        std::smatch build_id_match;
        if (std::regex_search(
                line,
                build_id_match,
                std::regex(R"(\bBuildId:\s*([0-9a-fA-F]+)\b)", std::regex_constants::icase)) &&
            build_id_match.size() > 1) {
            image.build_id = Trim(build_id_match[1].str());
        }
    }

    if (image.load_base.empty()) {
        std::smatch load_base_match;
        if (std::regex_search(
                line,
                load_base_match,
                std::regex(R"(\bload\s+base:\s*(0x[0-9a-fA-F]+)\b)", std::regex_constants::icase)) &&
            load_base_match.size() > 1) {
            image.load_base = Trim(load_base_match[1].str());
        }
    }
}

void ApplyToolOutput(const std::string& output, const std::string& tool_name, FrameResult& frame) {
    frame.symbol_text = Trim(output);
    if (frame.symbol_text.empty()) {
        return;
    }

    const std::vector<std::string> lines = SplitLines(frame.symbol_text);
    if (tool_name == "llvm-symbolizer") {
        if (!lines.empty()) {
            frame.function_name = lines[0];
        }
        if (lines.size() > 1) {
            ParseLocation(lines[1], frame);
        }
        return;
    }

    const std::string joined = lines.empty() ? frame.symbol_text : lines[0];
    const auto at = joined.find(" at ");
    if (at != std::string::npos) {
        frame.function_name = Trim(joined.substr(0, at));
        ParseLocation(joined.substr(at + 4), frame);
    } else if (!lines.empty()) {
        frame.function_name = lines[0];
    }
}

}  // namespace ndktrace

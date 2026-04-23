#pragma once

#include <string>
#include <vector>

#include "ndktrace/models.h"

namespace ndktrace {

struct ParsedFrame {
    bool matched = false;
    int frame_number = -1;
    std::string frame_kind;
    std::string frame_suffix;
    std::string address;
    std::string library_path;
    std::string library_name;
    std::string symbol_hint;
    std::string build_id;
    std::string file_offset;
    std::string load_base;
};

struct ParsedImageMetadata {
    bool matched = false;
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

std::string Trim(const std::string& value);
std::vector<std::string> SplitLines(const std::string& text);
std::string NormalizeAddress(const std::string& address);
std::string ExtractAbiValue(const std::string& line);
std::string ExtractSignalSummary(const std::string& line);
std::string ExtractTombstonePath(const std::string& line);
ParsedFrame ParseFrameLine(const std::string& line);
ParsedImageMetadata ParseMemoryMapLine(const std::string& line);
void ApplyMemoryMapMetadataLine(const std::string& line, ParsedImageMetadata& image);
void ApplyToolOutput(const std::string& output, const std::string& tool_name, FrameResult& frame);

}  // namespace ndktrace

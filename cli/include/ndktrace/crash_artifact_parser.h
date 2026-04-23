#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "ndktrace/models.h"

namespace ndktrace {

struct IndexedLine {
    std::size_t index = 0;
    std::string text;
};

struct ParsedCrashArtifact {
    CrashArtifactInfo artifact;
    std::vector<MemoryMapEntry> memory_maps;
    std::vector<IndexedLine> frame_lines;
};

ParsedCrashArtifact ParseCrashArtifactText(const std::string& text);

}  // namespace ndktrace


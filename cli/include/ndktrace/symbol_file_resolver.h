#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace ndktrace {

struct SymbolFileLookupRequest {
    std::filesystem::path so_input_path;
    std::string library_name;
    std::string library_path;
    std::string build_id;
    bool recursive = true;
    std::string match_mode = "basename";
};

struct SymbolFileLookupResult {
    std::optional<std::filesystem::path> path;
    std::string strategy;
    std::string file_build_id;
    bool had_build_id_mismatch = false;
};

SymbolFileLookupResult ResolveSymbolFile(const SymbolFileLookupRequest& request);

}  // namespace ndktrace

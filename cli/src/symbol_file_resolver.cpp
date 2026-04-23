#include "ndktrace/symbol_file_resolver.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <unordered_map>
#include <vector>

#include "ndktrace/elf_build_id.h"

namespace ndktrace {
namespace {

struct CandidateFile {
    std::filesystem::path path;
    bool exact_path_match = false;
    bool basename_match = false;
    std::optional<std::string> build_id;
};

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string NormalizeBuildId(std::string value) {
    value.erase(
        std::remove_if(value.begin(), value.end(), [](unsigned char ch) {
            return std::isspace(ch) != 0;
        }),
        value.end());
    if (value.rfind("0x", 0) == 0 || value.rfind("0X", 0) == 0) {
        value = value.substr(2);
    }
    return ToLower(value);
}

bool EndsWithNormalizedPath(const std::string& candidate, const std::string& raw_library_path) {
    std::string normalized_candidate = ToLower(candidate);
    std::string normalized_library = ToLower(raw_library_path);
    std::replace(normalized_candidate.begin(), normalized_candidate.end(), '\\', '/');
    std::replace(normalized_library.begin(), normalized_library.end(), '\\', '/');

    if (normalized_candidate == normalized_library) {
        return true;
    }
    if (normalized_library.empty() || normalized_candidate.size() < normalized_library.size()) {
        return false;
    }

    return normalized_candidate.compare(
               normalized_candidate.size() - normalized_library.size(),
               normalized_library.size(),
               normalized_library) == 0;
}

bool IsExactPathMatch(const std::filesystem::path& candidate, const std::string& library_path) {
    if (library_path.empty()) {
        return false;
    }
    return EndsWithNormalizedPath(candidate.generic_string(), library_path);
}

bool IsBasenameMatch(const std::filesystem::path& candidate, const std::string& library_name) {
    return !library_name.empty() && candidate.filename().string() == library_name;
}

std::optional<std::string> ReadCandidateBuildId(
    const std::filesystem::path& path,
    std::unordered_map<std::string, std::optional<std::string>>& cache) {
    const std::string key = path.generic_string();
    const auto cached = cache.find(key);
    if (cached != cache.end()) {
        return cached->second;
    }

    const auto build_id = ReadElfBuildId(path);
    cache.emplace(key, build_id);
    return build_id;
}

void AddCandidate(
    std::vector<CandidateFile>& candidates,
    const std::filesystem::path& path,
    const SymbolFileLookupRequest& request) {
    const bool exact_path_match = IsExactPathMatch(path, request.library_path);
    const bool basename_match = IsBasenameMatch(path, request.library_name);
    if (!exact_path_match && !basename_match) {
        return;
    }

    CandidateFile candidate;
    candidate.path = path;
    candidate.exact_path_match = exact_path_match;
    candidate.basename_match = basename_match;
    candidates.push_back(std::move(candidate));
}

std::vector<CandidateFile> CollectCandidates(const SymbolFileLookupRequest& request) {
    std::vector<CandidateFile> candidates;
    if (!std::filesystem::exists(request.so_input_path)) {
        return candidates;
    }

    if (std::filesystem::is_regular_file(request.so_input_path)) {
        AddCandidate(candidates, request.so_input_path, request);
        return candidates;
    }

    if (!std::filesystem::is_directory(request.so_input_path)) {
        return candidates;
    }

    if (request.recursive) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(request.so_input_path)) {
            if (entry.is_regular_file()) {
                AddCandidate(candidates, entry.path(), request);
            }
        }
        return candidates;
    }

    for (const auto& entry : std::filesystem::directory_iterator(request.so_input_path)) {
        if (entry.is_regular_file()) {
            AddCandidate(candidates, entry.path(), request);
        }
    }
    return candidates;
}

std::optional<CandidateFile*> FindBuildIdMatch(
    std::vector<CandidateFile>& candidates,
    const std::string& target_build_id,
    std::unordered_map<std::string, std::optional<std::string>>& cache,
    bool& had_mismatch) {
    if (target_build_id.empty()) {
        return std::nullopt;
    }

    const std::string normalized_target = NormalizeBuildId(target_build_id);
    CandidateFile* exact_path_match = nullptr;
    CandidateFile* basename_match = nullptr;
    CandidateFile* fallback_match = nullptr;

    for (auto& candidate : candidates) {
        candidate.build_id = ReadCandidateBuildId(candidate.path, cache);
        if (!candidate.build_id.has_value()) {
            continue;
        }

        if (NormalizeBuildId(*candidate.build_id) != normalized_target) {
            had_mismatch = true;
            continue;
        }

        if (candidate.exact_path_match) {
            exact_path_match = &candidate;
            break;
        }
        if (basename_match == nullptr && candidate.basename_match) {
            basename_match = &candidate;
        }
        if (fallback_match == nullptr) {
            fallback_match = &candidate;
        }
    }

    if (exact_path_match != nullptr) {
        return exact_path_match;
    }
    if (basename_match != nullptr) {
        return basename_match;
    }
    if (fallback_match != nullptr) {
        return fallback_match;
    }
    return std::nullopt;
}

bool CandidateHasKnownBuildIdMismatch(const CandidateFile& candidate, const std::string& target_build_id) {
    if (!candidate.build_id.has_value() || target_build_id.empty()) {
        return false;
    }
    return NormalizeBuildId(*candidate.build_id) != NormalizeBuildId(target_build_id);
}

std::optional<CandidateFile*> FindFallbackMatch(
    std::vector<CandidateFile>& candidates,
    const SymbolFileLookupRequest& request,
    std::unordered_map<std::string, std::optional<std::string>>& cache,
    bool& had_mismatch) {
    CandidateFile* exact_path_match = nullptr;
    CandidateFile* basename_match = nullptr;

    for (auto& candidate : candidates) {
        candidate.build_id = ReadCandidateBuildId(candidate.path, cache);
        if (CandidateHasKnownBuildIdMismatch(candidate, request.build_id)) {
            had_mismatch = true;
            continue;
        }

        if (candidate.exact_path_match) {
            exact_path_match = &candidate;
            break;
        }
        if (basename_match == nullptr && request.match_mode != "exact" && candidate.basename_match) {
            basename_match = &candidate;
        }
    }

    if (exact_path_match != nullptr) {
        return exact_path_match;
    }
    if (basename_match != nullptr) {
        return basename_match;
    }
    return std::nullopt;
}

std::string StrategyFor(const SymbolFileLookupRequest& request, const CandidateFile& candidate) {
    if (!request.build_id.empty() && candidate.build_id.has_value() &&
        NormalizeBuildId(request.build_id) == NormalizeBuildId(*candidate.build_id)) {
        return "build_id";
    }
    if (std::filesystem::is_regular_file(request.so_input_path)) {
        return "direct_file";
    }
    if (candidate.exact_path_match) {
        return "exact_path";
    }
    return "basename";
}

}  // namespace

SymbolFileLookupResult ResolveSymbolFile(const SymbolFileLookupRequest& request) {
    SymbolFileLookupResult result;
    std::vector<CandidateFile> candidates = CollectCandidates(request);
    if (candidates.empty()) {
        return result;
    }

    std::unordered_map<std::string, std::optional<std::string>> build_id_cache;
    if (const auto build_id_match = FindBuildIdMatch(candidates, request.build_id, build_id_cache, result.had_build_id_mismatch)) {
        result.path = (*build_id_match)->path;
        result.file_build_id = (*build_id_match)->build_id.value_or("");
        result.strategy = StrategyFor(request, *(*build_id_match));
        return result;
    }

    if (const auto fallback_match = FindFallbackMatch(candidates, request, build_id_cache, result.had_build_id_mismatch)) {
        result.path = (*fallback_match)->path;
        result.file_build_id = (*fallback_match)->build_id.value_or("");
        result.strategy = StrategyFor(request, *(*fallback_match));
    }

    return result;
}

}  // namespace ndktrace

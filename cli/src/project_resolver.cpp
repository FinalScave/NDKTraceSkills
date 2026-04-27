#include "ndktrace/project_resolver.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <utility>

#include "ndktrace/platform.h"

namespace ndktrace {
namespace {

struct ModuleSpec {
    std::string name;
    std::filesystem::path path;
    std::vector<std::filesystem::path> build_files;
    std::string ndk_version;
    std::optional<std::filesystem::path> ndk_path;
    bool has_native_build = false;
};

struct SymbolCandidateSpec {
    std::filesystem::path path;
    std::string library_name;
    std::string source;
    std::string module;
    std::string variant;
    std::string abi;
    bool stripped = false;
    int score = 0;
};

std::string Trim(const std::string& value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();
    if (first >= last) {
        return {};
    }
    return std::string(first, last);
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::optional<std::string> GetEnvironment(const char* name) {
#ifdef _WIN32
    char* buffer = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&buffer, &size, name) != 0 || buffer == nullptr || buffer[0] == '\0') {
        if (buffer != nullptr) {
            std::free(buffer);
        }
        return std::nullopt;
    }
    std::string value(buffer);
    std::free(buffer);
    return value;
#else
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return std::nullopt;
    }
    return std::string(value);
#endif
}

std::string NormalizeModuleName(const std::string& module_name) {
    const std::string cleaned = Trim(module_name);
    if (cleaned.empty()) {
        return {};
    }
    return cleaned[0] == ':' ? cleaned : ":" + cleaned;
}

std::string ModuleShortName(const std::string& module_name) {
    const auto last = module_name.find_last_of(':');
    if (last == std::string::npos) {
        return module_name;
    }
    return module_name.substr(last + 1);
}

bool HasBuildFile(const std::filesystem::path& path) {
    return std::filesystem::exists(path / "build.gradle") ||
        std::filesystem::exists(path / "build.gradle.kts");
}

std::optional<std::string> ReadTextIfExists(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) {
        return std::nullopt;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::string NormalizePathString(const std::filesystem::path& path) {
    std::error_code error;
    const auto normalized = std::filesystem::exists(path, error)
        ? std::filesystem::weakly_canonical(path, error)
        : path.lexically_normal();
    return ToGenericString(error ? path.lexically_normal() : normalized);
}

std::string DecodePropertiesValue(const std::string& value) {
    std::string decoded;
    decoded.reserve(value.size());

    std::size_t index = 0;
    while (index < value.size()) {
        const char ch = value[index];
        if (ch != '\\') {
            decoded.push_back(ch);
            ++index;
            continue;
        }

        ++index;
        if (index >= value.size()) {
            decoded.push_back('\\');
            break;
        }

        const char escaped = value[index];
        ++index;
        if (escaped == 'u' && index + 4 <= value.size()) {
            const std::string code = value.substr(index, 4);
            if (std::regex_match(code, std::regex(R"([0-9a-fA-F]{4})"))) {
                decoded.push_back(static_cast<char>(std::stoi(code, nullptr, 16)));
                index += 4;
                continue;
            }
            decoded.push_back('u');
            continue;
        }

        switch (escaped) {
            case 't':
                decoded.push_back('\t');
                break;
            case 'n':
                decoded.push_back('\n');
                break;
            case 'r':
                decoded.push_back('\r');
                break;
            case 'f':
                decoded.push_back('\f');
                break;
            default:
                decoded.push_back(escaped);
                break;
        }
    }

    return decoded;
}

std::map<std::string, std::string> ParseLocalProperties(const std::filesystem::path& project_root) {
    std::map<std::string, std::string> properties;
    const auto text = ReadTextIfExists(project_root / "local.properties");
    if (!text.has_value()) {
        return properties;
    }

    static const std::regex property_pattern(R"(^\s*([^:=\s][^:=]*)\s*[:=]\s*(.*)$)");
    std::istringstream input(*text);
    std::string raw_line;
    while (std::getline(input, raw_line)) {
        const std::string line = Trim(raw_line);
        if (line.empty() || line[0] == '#' || line[0] == '!') {
            continue;
        }

        std::smatch match;
        if (!std::regex_match(raw_line, match, property_pattern) || match.size() < 3) {
            continue;
        }
        properties[Trim(match[1].str())] = DecodePropertiesValue(Trim(match[2].str()));
    }

    return properties;
}

std::filesystem::path FindAndroidProjectRoot(const std::filesystem::path& input_path) {
    if (!std::filesystem::exists(input_path)) {
        throw std::runtime_error("Android project path does not exist: " + input_path.string());
    }

    std::filesystem::path current = std::filesystem::is_directory(input_path)
        ? input_path
        : input_path.parent_path();

    std::vector<std::filesystem::path> candidates;
    candidates.push_back(current);
    for (auto parent = current.parent_path(); !parent.empty() && parent != current; parent = parent.parent_path()) {
        candidates.push_back(parent);
        current = parent;
    }

    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate / "settings.gradle") ||
            std::filesystem::exists(candidate / "settings.gradle.kts")) {
            return candidate;
        }
    }

    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate / "local.properties")) {
            return candidate;
        }
    }

    return std::filesystem::is_directory(input_path) ? input_path : input_path.parent_path();
}

std::set<std::string> ParseIncludedModules(const std::string& settings_text) {
    std::set<std::string> modules;
    static const std::regex module_pattern(R"(['"](:[^'"]+)['"])");

    std::istringstream input(settings_text);
    std::string line;
    while (std::getline(input, line)) {
        const std::string trimmed = Trim(line);
        if (trimmed.rfind("include", 0) != 0) {
            continue;
        }

        for (std::sregex_iterator it(trimmed.begin(), trimmed.end(), module_pattern), end; it != end; ++it) {
            const std::string normalized = NormalizeModuleName((*it)[1].str());
            if (!normalized.empty()) {
                modules.insert(normalized);
            }
        }
    }

    return modules;
}

std::map<std::string, std::filesystem::path> ParseModuleDirectoryOverrides(
    const std::filesystem::path& project_root,
    const std::string& settings_text) {
    std::map<std::string, std::filesystem::path> overrides;
    static const std::regex pattern(
        R"(project\(\s*['"](:[^'"]+)['"]\s*\)\.projectDir\s*=\s*file\(\s*['"]([^'"]+)['"]\s*\))");

    for (std::sregex_iterator it(settings_text.begin(), settings_text.end(), pattern), end; it != end; ++it) {
        const std::string normalized = NormalizeModuleName((*it)[1].str());
        if (normalized.empty()) {
            continue;
        }
        overrides[normalized] = (project_root / (*it)[2].str()).lexically_normal();
    }

    return overrides;
}

std::filesystem::path DefaultModulePath(const std::filesystem::path& project_root, const std::string& module_name) {
    std::filesystem::path path = project_root;
    std::stringstream stream(module_name);
    std::string segment;
    while (std::getline(stream, segment, ':')) {
        if (!segment.empty()) {
            path /= segment;
        }
    }
    return path;
}

std::string ExtractFirstMatch(const std::string& text, const std::string& pattern) {
    std::smatch match;
    if (!std::regex_search(text, match, std::regex(pattern)) || match.size() < 2) {
        return {};
    }
    return Trim(match[1].str());
}

ModuleSpec BuildModuleSpec(const std::string& module_name, const std::filesystem::path& module_path) {
    ModuleSpec spec;
    spec.name = module_name;
    spec.path = module_path;
    for (const auto& build_file : {module_path / "build.gradle", module_path / "build.gradle.kts"}) {
        if (std::filesystem::exists(build_file)) {
            spec.build_files.push_back(build_file);
        }
    }

    std::string combined_text;
    for (const auto& build_file : spec.build_files) {
        const auto text = ReadTextIfExists(build_file);
        if (text.has_value()) {
            combined_text += *text;
            combined_text += '\n';
        }
    }

    spec.ndk_version = ExtractFirstMatch(combined_text, R"(ndkVersion\s*(?:=)?\s*['"]([^'"]+)['"])");
    const std::string raw_ndk_path = ExtractFirstMatch(combined_text, R"(ndkPath\s*(?:=)?\s*['"]([^'"]+)['"])");
    if (!raw_ndk_path.empty()) {
        std::filesystem::path candidate(raw_ndk_path);
        spec.ndk_path = candidate.is_absolute() ? candidate.lexically_normal() : (module_path / candidate).lexically_normal();
    }

    const std::string lowered = ToLower(combined_text);
    spec.has_native_build = !spec.ndk_version.empty() ||
        (lowered.find("externalnativebuild") != std::string::npos &&
            lowered.find("cmake") != std::string::npos &&
            lowered.find("path") != std::string::npos);

    return spec;
}

std::vector<ModuleSpec> DiscoverModules(
    const std::filesystem::path& project_root,
    const std::filesystem::path& input_path) {
    std::set<std::string> module_names;
    std::map<std::string, std::filesystem::path> overrides;

    for (const auto& settings_file : {project_root / "settings.gradle", project_root / "settings.gradle.kts"}) {
        const auto text = ReadTextIfExists(settings_file);
        if (!text.has_value()) {
            continue;
        }

        const auto modules = ParseIncludedModules(*text);
        module_names.insert(modules.begin(), modules.end());
        const auto settings_overrides = ParseModuleDirectoryOverrides(project_root, *text);
        overrides.insert(settings_overrides.begin(), settings_overrides.end());
    }

    if (module_names.empty() && HasBuildFile(project_root)) {
        module_names.insert(":" + project_root.filename().string());
    }

    std::vector<ModuleSpec> specs;
    for (const auto& module_name : module_names) {
        const auto override_it = overrides.find(module_name);
        const auto module_path = override_it != overrides.end()
            ? override_it->second
            : DefaultModulePath(project_root, module_name);
        if (!std::filesystem::exists(module_path) || !HasBuildFile(module_path)) {
            continue;
        }
        specs.push_back(BuildModuleSpec(module_name, module_path));
    }

    if (HasBuildFile(input_path)) {
        const bool already_present = std::any_of(specs.begin(), specs.end(), [&input_path](const ModuleSpec& spec) {
            return spec.path == input_path;
        });
        if (!already_present) {
            specs.push_back(BuildModuleSpec(":" + input_path.filename().string(), input_path));
        }
    }

    std::sort(specs.begin(), specs.end(), [](const ModuleSpec& left, const ModuleSpec& right) {
        return left.name < right.name;
    });
    return specs;
}

std::filesystem::path ResolveSdkDir(const std::filesystem::path& project_root) {
    const auto properties = ParseLocalProperties(project_root);
    const auto sdk_it = properties.find("sdk.dir");
    if (sdk_it != properties.end()) {
        std::filesystem::path candidate(sdk_it->second);
        if (std::filesystem::exists(candidate)) {
            return candidate.lexically_normal();
        }
    }

    for (const char* env_name : {"ANDROID_SDK_ROOT", "ANDROID_HOME"}) {
        const auto value = GetEnvironment(env_name);
        if (value.has_value()) {
            const std::filesystem::path candidate(*value);
            if (std::filesystem::exists(candidate)) {
                return candidate.lexically_normal();
            }
        }
    }

    if (const auto local_app_data = GetEnvironment("LOCALAPPDATA")) {
        const std::filesystem::path candidate(*local_app_data + "\\Android\\Sdk");
        if (std::filesystem::exists(candidate)) {
            return candidate.lexically_normal();
        }
    }
    if (const auto user_profile = GetEnvironment("USERPROFILE")) {
        const std::filesystem::path candidate(*user_profile + "\\AppData\\Local\\Android\\Sdk");
        if (std::filesystem::exists(candidate)) {
            return candidate.lexically_normal();
        }
    }
    if (const auto home = GetEnvironment("HOME")) {
        for (const auto& candidate : {
                 std::filesystem::path(*home + "/Android/Sdk"),
                 std::filesystem::path(*home + "/Library/Android/sdk")}) {
            if (std::filesystem::exists(candidate)) {
                return candidate.lexically_normal();
            }
        }
    }

    return {};
}

std::vector<std::string> SplitVersionTokens(const std::string& value) {
    std::vector<std::string> tokens;
    static const std::regex token_pattern(R"((\d+|[^\d]+))");
    for (std::sregex_iterator it(value.begin(), value.end(), token_pattern), end; it != end; ++it) {
        tokens.push_back((*it)[1].str());
    }
    return tokens;
}

bool CompareVersionNamesDescending(const std::filesystem::path& left, const std::filesystem::path& right) {
    const auto left_tokens = SplitVersionTokens(left.filename().string());
    const auto right_tokens = SplitVersionTokens(right.filename().string());
    const std::size_t limit = std::max(left_tokens.size(), right_tokens.size());
    for (std::size_t index = 0; index < limit; ++index) {
        if (index >= left_tokens.size()) {
            return false;
        }
        if (index >= right_tokens.size()) {
            return true;
        }

        const bool left_numeric = std::all_of(left_tokens[index].begin(), left_tokens[index].end(), [](unsigned char ch) {
            return std::isdigit(ch) != 0;
        });
        const bool right_numeric = std::all_of(right_tokens[index].begin(), right_tokens[index].end(), [](unsigned char ch) {
            return std::isdigit(ch) != 0;
        });

        if (left_numeric && right_numeric) {
            const long long left_value = std::stoll(left_tokens[index]);
            const long long right_value = std::stoll(right_tokens[index]);
            if (left_value != right_value) {
                return left_value > right_value;
            }
            continue;
        }

        const std::string left_lower = ToLower(left_tokens[index]);
        const std::string right_lower = ToLower(right_tokens[index]);
        if (left_lower != right_lower) {
            return left_lower > right_lower;
        }
    }

    return left.filename().string() > right.filename().string();
}

std::vector<std::filesystem::path> DiscoverLocalNdkCandidates() {
    auto candidates = DiscoverNdkCandidates();
    std::sort(candidates.begin(), candidates.end(), CompareVersionNamesDescending);
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    return candidates;
}

std::vector<std::string> ExtractRequestedLibraryNames(
    const std::string& library_name,
    const std::string& crash_text) {
    std::vector<std::string> names;
    const std::string explicit_name = Trim(library_name);
    if (!explicit_name.empty()) {
        names.push_back(std::filesystem::path(explicit_name).filename().string());
    }

    if (!crash_text.empty()) {
        static const std::regex pattern(R"(([^\s()]+\.so)\b)");
        for (std::sregex_iterator it(crash_text.begin(), crash_text.end(), pattern), end; it != end; ++it) {
            const std::string basename = std::filesystem::path((*it)[1].str()).filename().string();
            if (!basename.empty() && std::find(names.begin(), names.end(), basename) == names.end()) {
                names.push_back(basename);
            }
        }
    }

    return names;
}

bool PathMatchesVariant(const std::filesystem::path& path, const std::string& variant) {
    const std::string trimmed_variant = Trim(variant);
    if (trimmed_variant.empty()) {
        return true;
    }
    return ToLower(path.generic_string()).find(ToLower(trimmed_variant)) != std::string::npos;
}

bool PathMatchesAbi(const std::filesystem::path& path, const std::string& abi) {
    const std::string trimmed_abi = Trim(abi);
    if (trimmed_abi.empty()) {
        return true;
    }
    return ToLower(path.generic_string()).find("/" + ToLower(trimmed_abi) + "/") != std::string::npos;
}

std::vector<SymbolCandidateSpec> CollectSymbolCandidates(
    const ModuleSpec& module_spec,
    const std::string& variant,
    const std::string& abi,
    const std::vector<std::string>& requested_libraries) {
    const std::filesystem::path build_dir = module_spec.path / "build";
    const std::vector<std::tuple<std::string, std::filesystem::path, bool, int>> search_roots = {
        {"cxx_obj", build_dir / "intermediates" / "cxx", false, 500},
        {"cmake_obj", build_dir / "intermediates" / "cmake", false, 460},
        {"external_native_build", build_dir / "intermediates" / "externalNativeBuild", false, 420},
        {"library_jni", build_dir / "intermediates" / "library_jni", false, 260},
        {"library_and_local_jars_jni", build_dir / "intermediates" / "library_and_local_jars_jni", false, 250},
        {"merged_native_libs", build_dir / "intermediates" / "merged_native_libs", false, 180},
        {"merged_jni_libs", build_dir / "intermediates" / "merged_jni_libs", false, 170},
        {"stripped_native_libs", build_dir / "intermediates" / "stripped_native_libs", true, 80},
    };

    std::set<std::string> requested_set(requested_libraries.begin(), requested_libraries.end());
    std::unordered_map<std::string, SymbolCandidateSpec> deduped;

    for (const auto& [source, root, stripped, base_score] : search_roots) {
        if (!std::filesystem::exists(root) || !std::filesystem::is_directory(root)) {
            continue;
        }

        std::error_code error;
        for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, error), end;
             it != end;
             it.increment(error)) {
            if (error || !it->is_regular_file() || it->path().extension() != ".so") {
                continue;
            }
            if (!PathMatchesVariant(it->path(), variant) || !PathMatchesAbi(it->path(), abi)) {
                continue;
            }

            const std::string library_basename = it->path().filename().string();
            if (!requested_set.empty() && requested_set.count(library_basename) == 0) {
                continue;
            }

            int score = base_score;
            if (ToLower(it->path().generic_string()).find("/obj/") != std::string::npos) {
                score += 40;
            }
            if (!requested_set.empty()) {
                score += 200;
            }
            if (module_spec.has_native_build) {
                score += 50;
            }

            SymbolCandidateSpec candidate;
            candidate.path = it->path();
            candidate.library_name = library_basename;
            candidate.source = source;
            candidate.module = ModuleShortName(module_spec.name);
            candidate.variant = variant;
            candidate.abi = abi;
            candidate.stripped = stripped;
            candidate.score = score;

            const std::string key = NormalizePathString(candidate.path);
            const auto existing = deduped.find(key);
            if (existing == deduped.end() || candidate.score > existing->second.score) {
                deduped[key] = candidate;
            }
        }
    }

    std::vector<SymbolCandidateSpec> candidates;
    for (const auto& entry : deduped) {
        candidates.push_back(entry.second);
    }
    std::sort(candidates.begin(), candidates.end(), [](const SymbolCandidateSpec& left, const SymbolCandidateSpec& right) {
        if (left.score != right.score) {
            return left.score > right.score;
        }
        return left.path.generic_string() < right.path.generic_string();
    });
    return candidates;
}

std::filesystem::path ChoosePreferredSymbolPath(
    const std::vector<SymbolCandidateSpec>& candidates,
    const std::vector<std::string>& requested_libraries) {
    if (candidates.empty()) {
        return {};
    }

    if (!requested_libraries.empty()) {
        std::set<std::string> requested_set(requested_libraries.begin(), requested_libraries.end());
        std::vector<SymbolCandidateSpec> matching;
        for (const auto& candidate : candidates) {
            if (requested_set.count(candidate.library_name) != 0) {
                matching.push_back(candidate);
            }
        }
        if (matching.empty()) {
            return {};
        }
        if (requested_libraries.size() == 1) {
            return matching.front().path;
        }

        std::set<std::filesystem::path> parents;
        for (const auto& candidate : matching) {
            parents.insert(candidate.path.parent_path());
        }
        return parents.size() == 1 ? *parents.begin() : matching.front().path;
    }

    const SymbolCandidateSpec& top_candidate = candidates.front();
    if (top_candidate.source == "cxx_obj" ||
        top_candidate.source == "cmake_obj" ||
        top_candidate.source == "external_native_build") {
        return top_candidate.path.parent_path();
    }
    return top_candidate.path;
}

const ModuleSpec* SelectModuleSpec(
    const std::vector<ModuleSpec>& module_specs,
    const std::filesystem::path& input_path,
    const std::string& explicit_module,
    const std::string& variant,
    const std::string& abi,
    const std::vector<std::string>& requested_libraries,
    std::vector<std::string>& ambiguities,
    std::unordered_map<std::string, std::vector<SymbolCandidateSpec>>& candidate_cache) {
    if (module_specs.empty()) {
        return nullptr;
    }

    const std::string requested_module = NormalizeModuleName(explicit_module);
    if (!requested_module.empty()) {
        for (const auto& spec : module_specs) {
            if (spec.name == requested_module || ModuleShortName(spec.name) == requested_module.substr(1)) {
                return &spec;
            }
        }
        throw std::runtime_error("Android module was not found: " + explicit_module);
    }

    for (const auto& spec : module_specs) {
        if (spec.path == input_path && spec.has_native_build) {
            return &spec;
        }
    }

    std::vector<const ModuleSpec*> preferred_pool;
    for (const auto& spec : module_specs) {
        if (spec.has_native_build) {
            preferred_pool.push_back(&spec);
        }
    }
    std::vector<const ModuleSpec*> fallback_pool;
    if (preferred_pool.empty()) {
        for (const auto& spec : module_specs) {
            fallback_pool.push_back(&spec);
        }
    }
    const std::vector<const ModuleSpec*>& candidate_pool = preferred_pool.empty() ? fallback_pool : preferred_pool;

    if (!requested_libraries.empty()) {
        std::vector<std::pair<const ModuleSpec*, std::vector<SymbolCandidateSpec>>> ranked;
        auto collect_for = [&](const ModuleSpec& spec) {
            auto candidates = CollectSymbolCandidates(spec, variant, abi, requested_libraries);
            candidate_cache[spec.name] = candidates;
            if (!candidates.empty()) {
                ranked.push_back({&spec, std::move(candidates)});
            }
        };

        for (const auto* spec : candidate_pool) {
            collect_for(*spec);
        }
        if (ranked.empty() && !preferred_pool.empty()) {
            for (const auto& spec : module_specs) {
                if (candidate_cache.find(spec.name) == candidate_cache.end()) {
                    collect_for(spec);
                }
            }
        }

        if (!ranked.empty()) {
            std::sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) {
                if (left.second.front().score != right.second.front().score) {
                    return left.second.front().score > right.second.front().score;
                }
                if (left.second.size() != right.second.size()) {
                    return left.second.size() > right.second.size();
                }
                return left.first->name < right.first->name;
            });
            if (ranked.size() > 1 && ranked[0].second.front().score == ranked[1].second.front().score) {
                ambiguities.push_back(
                    "Multiple Android modules expose matching native outputs. Using " +
                    ModuleShortName(ranked[0].first->name) + ".");
            }
            return ranked.front().first;
        }
    }

    if (candidate_pool.size() == 1) {
        return candidate_pool.front();
    }
    if (!candidate_pool.empty()) {
        ambiguities.push_back(
            "Multiple Android modules were discovered. Using " +
            ModuleShortName(candidate_pool.front()->name) + ".");
        return candidate_pool.front();
    }

    return &module_specs.front();
}

std::pair<std::string, std::filesystem::path> ResolveNdkPath(
    const std::filesystem::path& sdk_dir,
    const ModuleSpec* module_spec,
    const std::vector<ModuleSpec>& module_specs,
    std::vector<std::string>& warnings,
    std::vector<std::string>& ambiguities) {
    std::string ndk_version = module_spec != nullptr ? module_spec->ndk_version : std::string();
    std::filesystem::path ndk_path;
    if (module_spec != nullptr && module_spec->ndk_path.has_value()) {
        ndk_path = *module_spec->ndk_path;
    }

    if (ndk_version.empty()) {
        std::set<std::string> discovered_versions;
        for (const auto& spec : module_specs) {
            if (!spec.ndk_version.empty()) {
                discovered_versions.insert(spec.ndk_version);
            }
        }
        if (discovered_versions.size() == 1) {
            ndk_version = *discovered_versions.begin();
        } else if (discovered_versions.size() > 1) {
            ambiguities.push_back("Multiple ndkVersion declarations were found across modules.");
        }
    }

    if (!ndk_path.empty() && IsValidNdkDirectory(ndk_path)) {
        return {ndk_version, ndk_path};
    }

    if (!sdk_dir.empty() && !ndk_version.empty()) {
        const std::filesystem::path candidate = sdk_dir / "ndk" / ndk_version;
        if (IsValidNdkDirectory(candidate)) {
            return {ndk_version, candidate};
        }
        warnings.push_back("Declared ndkVersion " + ndk_version + " was not found under sdk.dir.");
    }

    auto local_candidates = DiscoverLocalNdkCandidates();
    if (!ndk_version.empty()) {
        for (const auto& candidate : local_candidates) {
            if (candidate.filename().string() == ndk_version) {
                return {ndk_version, candidate};
            }
        }
    }

    if (!sdk_dir.empty()) {
        const std::filesystem::path ndk_root = sdk_dir / "ndk";
        if (std::filesystem::exists(ndk_root) && std::filesystem::is_directory(ndk_root)) {
            std::vector<std::filesystem::path> installed;
            for (const auto& entry : std::filesystem::directory_iterator(ndk_root)) {
                if (entry.is_directory() && IsValidNdkDirectory(entry.path())) {
                    installed.push_back(entry.path());
                }
            }
            std::sort(installed.begin(), installed.end(), CompareVersionNamesDescending);
            if (installed.size() == 1) {
                return {ndk_version.empty() ? installed[0].filename().string() : ndk_version, installed[0]};
            }
            if (!installed.empty()) {
                warnings.push_back("Using the newest installed NDK under sdk.dir because no exact match was found.");
                return {ndk_version.empty() ? installed[0].filename().string() : ndk_version, installed[0]};
            }
        }
    }

    if (local_candidates.size() == 1) {
        return {ndk_version.empty() ? local_candidates[0].filename().string() : ndk_version, local_candidates[0]};
    }
    if (local_candidates.size() > 1) {
        ambiguities.push_back("Multiple local NDK installations were found.");
        return {ndk_version, local_candidates[0]};
    }

    return {ndk_version, {}};
}

std::string DetermineStatus(const ProjectResolutionContext& context) {
    if (!context.errors.empty()) {
        const bool has_any_signal =
            !context.module_candidates.empty() ||
            !context.symbol_candidates.empty() ||
            !context.ndk_path.empty() ||
            !context.preferred_symbol_path.empty() ||
            !context.module_name.empty();
        return has_any_signal ? "partial" : "not_found";
    }
    if (!context.ambiguities.empty()) {
        return "ambiguous";
    }
    return "resolved";
}

void PopulateModuleCandidates(
    const std::vector<ModuleSpec>& module_specs,
    ProjectResolutionContext& context) {
    for (const auto& spec : module_specs) {
        ProjectModuleCandidate candidate;
        candidate.name = spec.name;
        candidate.path = NormalizePathString(spec.path);
        candidate.has_native_build = spec.has_native_build;
        candidate.ndk_version = spec.ndk_version;
        if (spec.ndk_path.has_value()) {
            candidate.ndk_path = NormalizePathString(*spec.ndk_path);
        }
        context.module_candidates.push_back(candidate);
    }
}

void PopulateSymbolCandidates(
    const std::vector<SymbolCandidateSpec>& symbols,
    ProjectResolutionContext& context) {
    const std::size_t limit = std::min<std::size_t>(symbols.size(), 20);
    for (std::size_t index = 0; index < limit; ++index) {
        ProjectSymbolCandidate candidate;
        candidate.path = NormalizePathString(symbols[index].path);
        candidate.library_name = symbols[index].library_name;
        candidate.source = symbols[index].source;
        candidate.module = symbols[index].module;
        candidate.variant = symbols[index].variant;
        candidate.abi = symbols[index].abi;
        candidate.stripped = symbols[index].stripped;
        candidate.score = symbols[index].score;
        context.symbol_candidates.push_back(candidate);
    }
    if (symbols.size() > limit) {
        context.warnings.push_back(
            "Only the top " + std::to_string(limit) + " symbol candidates are included out of " +
            std::to_string(symbols.size()) + " matches.");
    }
}

}  // namespace

ProjectResolutionContext ResolveAndroidProject(
    const ProjectResolverRequest& request,
    const std::string& crash_text) {
    ProjectResolutionContext context;
    context.attempted = true;
    context.variant = request.variant.empty() ? "Debug" : request.variant;
    context.abi = request.abi.empty() ? "arm64-v8a" : request.abi;

    const std::string project_path_value = Trim(request.project_path);
    if (project_path_value.empty()) {
        context.status = "not_requested";
        return context;
    }

    try {
        const std::filesystem::path input_path = std::filesystem::absolute(project_path_value);
        context.input_path = NormalizePathString(input_path);

        const std::filesystem::path project_root = FindAndroidProjectRoot(input_path);
        context.project_path = NormalizePathString(project_root);

        const auto module_specs = DiscoverModules(project_root, input_path);
        PopulateModuleCandidates(module_specs, context);
        if (module_specs.empty()) {
            context.errors.push_back("No Android Gradle modules were found under the provided project path.");
        }

        const auto requested_libraries = ExtractRequestedLibraryNames(request.library_name, crash_text);
        context.library_names = requested_libraries;

        std::unordered_map<std::string, std::vector<SymbolCandidateSpec>> candidate_cache;
        const ModuleSpec* selected_module = module_specs.empty()
            ? nullptr
            : SelectModuleSpec(
                  module_specs,
                  input_path,
                  request.module_name,
                  context.variant,
                  context.abi,
                  requested_libraries,
                  context.ambiguities,
                  candidate_cache);

        if (selected_module != nullptr) {
            context.module = ModuleShortName(selected_module->name);
            context.module_name = selected_module->name;
            context.module_path = NormalizePathString(selected_module->path);
        }

        const std::filesystem::path sdk_dir = ResolveSdkDir(project_root);
        if (!sdk_dir.empty()) {
            context.sdk_dir = NormalizePathString(sdk_dir);
        } else {
            context.warnings.push_back("Android sdk.dir was not found in local.properties or the environment.");
        }

        const auto [ndk_version, ndk_path] = ResolveNdkPath(
            sdk_dir,
            selected_module,
            module_specs,
            context.warnings,
            context.ambiguities);
        context.ndk_version = ndk_version;
        if (!ndk_path.empty()) {
            context.ndk_path = NormalizePathString(ndk_path);
        } else {
            context.errors.push_back("Unable to resolve an Android NDK path from the project or local environment.");
        }

        std::vector<SymbolCandidateSpec> symbol_candidates;
        if (selected_module != nullptr) {
            const auto cached = candidate_cache.find(selected_module->name);
            symbol_candidates = cached != candidate_cache.end()
                ? cached->second
                : CollectSymbolCandidates(*selected_module, context.variant, context.abi, requested_libraries);
        }

        if (requested_libraries.empty() && !symbol_candidates.empty()) {
            std::set<std::string> unique_libraries;
            for (const auto& candidate : symbol_candidates) {
                unique_libraries.insert(candidate.library_name);
            }
            if (unique_libraries.size() > 1) {
                context.ambiguities.push_back(
                    "Multiple native libraries were found for the selected module. Provide --library or a crash trace.");
            }
        }

        const std::filesystem::path preferred_symbol_path = ChoosePreferredSymbolPath(symbol_candidates, requested_libraries);
        if (!preferred_symbol_path.empty()) {
            context.preferred_symbol_path = NormalizePathString(preferred_symbol_path);
        } else if (!requested_libraries.empty()) {
            context.errors.push_back("Unable to resolve a native symbol path from the Android build outputs.");
        } else {
            context.warnings.push_back("No matching native symbol output was found under the module build directory.");
        }
        PopulateSymbolCandidates(symbol_candidates, context);

        context.status = DetermineStatus(context);
        context.ok = context.status == "resolved";
    } catch (const std::exception& ex) {
        context.errors.push_back(ex.what());
        context.status = "not_found";
        context.ok = false;
    }

    return context;
}

}  // namespace ndktrace

#include "ndktrace/platform.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>

#ifdef _WIN32
#include <cstdio>
#else
#include <cstdio>
#endif

namespace ndktrace {
namespace {

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

void AddIfValid(std::set<std::filesystem::path>& out, const std::filesystem::path& candidate) {
    if (!candidate.empty() && std::filesystem::exists(candidate) && IsValidNdkDirectory(candidate)) {
        out.insert(std::filesystem::weakly_canonical(candidate));
    }
}

void AddChildrenIfValid(std::set<std::filesystem::path>& out, const std::filesystem::path& root) {
    if (root.empty() || !std::filesystem::exists(root) || !std::filesystem::is_directory(root)) {
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (entry.is_directory()) {
            AddIfValid(out, entry.path());
        }
    }
}

std::vector<std::filesystem::path> CommonNdkRoots() {
    std::vector<std::filesystem::path> roots;

    if (const auto local_app_data = GetEnvironment("LOCALAPPDATA")) {
        roots.emplace_back(*local_app_data + "\\Android\\Sdk\\ndk");
    }
    if (const auto user_profile = GetEnvironment("USERPROFILE")) {
        roots.emplace_back(*user_profile + "\\Android\\Sdk\\ndk");
        roots.emplace_back(*user_profile + "\\AppData\\Local\\Android\\Sdk\\ndk");
    }
    if (const auto home = GetEnvironment("HOME")) {
        roots.emplace_back(*home + "/Android/Sdk/ndk");
        roots.emplace_back(*home + "/Library/Android/sdk/ndk");
    }

    roots.emplace_back("C:\\Android\\ndk");
    roots.emplace_back("D:\\Android\\ndk");
    roots.emplace_back("/usr/local/android-ndk");
    roots.emplace_back("/opt/android-ndk");

    return roots;
}

std::string QuoteArgument(const std::string& value) {
    if (value.find_first_of(" \t\"") == std::string::npos) {
        return value;
    }

    std::string quoted = "\"";
    for (char ch : value) {
        if (ch == '"') {
            quoted += "\\\"";
        } else {
            quoted += ch;
        }
    }
    quoted += "\"";
    return quoted;
}

}  // namespace

std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::string ReadStdIn() {
    std::ostringstream buffer;
    buffer << std::cin.rdbuf();
    return buffer.str();
}

bool IsValidNdkDirectory(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path) || !std::filesystem::is_directory(path)) {
        return false;
    }

    const bool has_toolchains = std::filesystem::exists(path / "toolchains");
    const bool has_build = std::filesystem::exists(path / "build");
    const bool has_source_properties = std::filesystem::exists(path / "source.properties");
    const bool has_ndk_build = std::filesystem::exists(path / "ndk-build") ||
        std::filesystem::exists(path / "ndk-build.cmd");

    return has_toolchains && has_build && (has_source_properties || has_ndk_build);
}

std::vector<std::filesystem::path> DiscoverNdkCandidates() {
    std::set<std::filesystem::path> found;

    const std::array<const char*, 4> envs = {"ANDROID_NDK_HOME", "ANDROID_NDK_ROOT", "NDK_HOME", "NDK_ROOT"};
    for (const char* env_name : envs) {
        if (const auto value = GetEnvironment(env_name)) {
            const auto candidate = std::filesystem::path(*value);
            AddIfValid(found, candidate);
            AddChildrenIfValid(found, candidate);
        }
    }

    for (const auto& root : CommonNdkRoots()) {
        AddIfValid(found, root);
        AddChildrenIfValid(found, root);
    }

    return {found.begin(), found.end()};
}

ToolchainInfo ResolveToolchain(const std::filesystem::path& ndk_path) {
    ToolchainInfo info;
    info.ndk_path = ToGenericString(ndk_path);

    const auto prebuilt_root = ndk_path / "toolchains" / "llvm" / "prebuilt";
    if (!std::filesystem::exists(prebuilt_root) || !std::filesystem::is_directory(prebuilt_root)) {
        return info;
    }

    for (const auto& host_dir : std::filesystem::directory_iterator(prebuilt_root)) {
        if (!host_dir.is_directory()) {
            continue;
        }
        const auto bin_dir = host_dir.path() / "bin";
        const auto symbolizer = std::filesystem::exists(bin_dir / "llvm-symbolizer.exe")
            ? bin_dir / "llvm-symbolizer.exe"
            : bin_dir / "llvm-symbolizer";
        const auto addr2line = std::filesystem::exists(bin_dir / "llvm-addr2line.exe")
            ? bin_dir / "llvm-addr2line.exe"
            : bin_dir / "llvm-addr2line";

        if (std::filesystem::exists(symbolizer)) {
            info.symbolizer_path = ToGenericString(symbolizer);
            info.has_symbolizer = true;
        }
        if (std::filesystem::exists(addr2line)) {
            info.addr2line_path = ToGenericString(addr2line);
            info.has_addr2line = true;
        }
        if (info.has_symbolizer || info.has_addr2line) {
            break;
        }
    }

    if (info.has_symbolizer) {
        info.selected_tool = "llvm-symbolizer";
    } else if (info.has_addr2line) {
        info.selected_tool = "llvm-addr2line";
    }

    return info;
}

ProcessResult RunProcess(const std::vector<std::string>& args) {
    ProcessResult result;
    if (args.empty()) {
        result.output = "No command provided";
        return result;
    }

    std::ostringstream command;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i != 0) {
            command << ' ';
        }
        command << QuoteArgument(args[i]);
    }
    command << " 2>&1";

#ifdef _WIN32
    FILE* pipe = _popen(command.str().c_str(), "r");
#else
    FILE* pipe = popen(command.str().c_str(), "r");
#endif
    if (pipe == nullptr) {
        result.output = "Failed to start process";
        return result;
    }

    std::array<char, 4096> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result.output += buffer.data();
    }

#ifdef _WIN32
    result.exit_code = _pclose(pipe);
#else
    result.exit_code = pclose(pipe);
#endif
    result.output = Trim(result.output);
    return result;
}

std::string ToGenericString(const std::filesystem::path& path) {
    return path.generic_string();
}

}  // namespace ndktrace

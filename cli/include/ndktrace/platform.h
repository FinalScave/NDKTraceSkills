#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "ndktrace/models.h"

namespace ndktrace {

std::string ReadTextFile(const std::filesystem::path& path);
std::string ReadStdIn();

bool IsValidNdkDirectory(const std::filesystem::path& path);
std::vector<std::filesystem::path> DiscoverNdkCandidates();
ToolchainInfo ResolveToolchain(const std::filesystem::path& ndk_path);

ProcessResult RunProcess(const std::vector<std::string>& args);
std::string ToGenericString(const std::filesystem::path& path);

}  // namespace ndktrace

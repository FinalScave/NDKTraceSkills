#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace ndktrace {

std::optional<std::string> ReadElfBuildId(const std::filesystem::path& path);

}  // namespace ndktrace

#pragma once

#include <string>

#include "ndktrace/models.h"

namespace ndktrace {

struct ProjectResolverRequest {
    std::string project_path;
    std::string module_name;
    std::string variant = "Debug";
    std::string abi = "arm64-v8a";
    std::string library_name;
};

ProjectResolutionContext ResolveAndroidProject(
    const ProjectResolverRequest& request,
    const std::string& crash_text);

}  // namespace ndktrace

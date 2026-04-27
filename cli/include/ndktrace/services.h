#pragma once

#include "ndktrace/models.h"

namespace ndktrace {

ResolveProjectResult RunResolveProject(const ResolveProjectRequest& request);
RestoreResult RunRestore(const RestoreRequest& request);
ScanResult RunScan(const ScanRequest& request);
ValidateResult RunValidate(const ValidateRequest& request);

}  // namespace ndktrace

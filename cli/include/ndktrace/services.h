#pragma once

#include "ndktrace/models.h"

namespace ndktrace {

RestoreResult RunRestore(const RestoreRequest& request);
ScanResult RunScan(const ScanRequest& request);
ValidateResult RunValidate(const ValidateRequest& request);

}  // namespace ndktrace


#pragma once

#include <string>

#include "ndktrace/models.h"

namespace ndktrace {

std::string ToJson(const RestoreResult& result, bool pretty);
std::string ToJson(const ScanResult& result, bool pretty);
std::string ToJson(const ValidateResult& result, bool pretty);

}  // namespace ndktrace


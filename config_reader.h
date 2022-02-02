#pragma once

#include <string>

namespace common {
namespace config {

bool watchDirectory(const std::string& path);

std::string load(const std::string& file, const std::string& key);

} // namespace config
} // namespace common

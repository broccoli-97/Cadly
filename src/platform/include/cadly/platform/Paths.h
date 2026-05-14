#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cadly::platform {

// Resolves the directory containing a runtime asset relative to the executable
// or the build tree. The lookup tries, in order:
//   1. CADLY_ASSET_ROOT environment variable
//   2. <exe-dir>/<subdir>
//   3. <exe-dir>/../share/cadly/<subdir>
//   4. <exe-dir>/../<subdir>          (handy in build trees)
//   5. <source-root>/<subdir>         (only if CADLY_SOURCE_ROOT is defined)
std::optional<std::filesystem::path>
find_asset_dir(const std::string& subdir);

// Returns the directory containing the running executable. Falls back to
// current_path() on platforms where /proc isn't available.
std::filesystem::path executable_dir();

// Read a UTF-8 text file. Returns std::nullopt if the file can't be opened.
std::optional<std::string>
read_text_file(const std::filesystem::path& path);

} // namespace cadly::platform

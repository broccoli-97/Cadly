#include "cadly/platform/Paths.h"
#include "cadly/platform/Log.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

#if defined(__linux__)
#  include <unistd.h>
#elif defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#elif defined(__APPLE__)
#  include <mach-o/dyld.h>
#endif

namespace fs = std::filesystem;

namespace cadly::platform {

namespace {

fs::path read_exe_path() {
#if defined(__linux__)
  char buf[4096] = {};
  const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n > 0) {
    buf[n] = '\0';
    return fs::path(buf);
  }
#elif defined(_WIN32)
  wchar_t buf[MAX_PATH] = {};
  if (::GetModuleFileNameW(nullptr, buf, MAX_PATH) > 0) {
    return fs::path(buf);
  }
#elif defined(__APPLE__)
  char buf[4096] = {};
  uint32_t size = sizeof(buf);
  if (_NSGetExecutablePath(buf, &size) == 0) {
    return fs::path(buf);
  }
#endif
  std::error_code ec;
  return fs::current_path(ec);
}

} // namespace

fs::path executable_dir() {
  const auto exe = read_exe_path();
  return exe.has_parent_path() ? exe.parent_path() : exe;
}

std::optional<fs::path> find_asset_dir(const std::string& subdir) {
  std::vector<fs::path> candidates;
  if (const char* env = std::getenv("CADLY_ASSET_ROOT")) {
    candidates.emplace_back(fs::path(env) / subdir);
  }
  const auto exe_dir = executable_dir();
  candidates.emplace_back(exe_dir / subdir);
  candidates.emplace_back(exe_dir / ".." / "share" / "cadly" / subdir);
  candidates.emplace_back(exe_dir / ".." / subdir);
#ifdef CADLY_SOURCE_ROOT
  candidates.emplace_back(fs::path(CADLY_SOURCE_ROOT) / subdir);
#endif

  std::error_code ec;
  for (const auto& p : candidates) {
    if (fs::is_directory(p, ec)) {
      return fs::canonical(p, ec);
    }
  }
  CADLY_LOG_WARN("Asset directory '{}' not found in any candidate location.", subdir);
  return std::nullopt;
}

std::optional<std::string>
read_text_file(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return std::nullopt;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

} // namespace cadly::platform

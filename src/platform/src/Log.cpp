#include "cadly/platform/Log.h"

#include <spdlog/sinks/stdout_color_sinks.h>

#include <memory>
#include <mutex>

namespace cadly::platform {
namespace {

std::shared_ptr<spdlog::logger>& global_logger() {
  static std::shared_ptr<spdlog::logger> instance;
  return instance;
}

std::once_flag g_init_flag;

} // namespace

void init_logging(const char* level_name) {
  std::call_once(g_init_flag, []() {
    auto logger = spdlog::stdout_color_mt("cadly");
    logger->set_pattern("[%H:%M:%S.%e] [%n] [%^%l%$] %v");
    global_logger() = std::move(logger);
  });

  if (auto& logger = global_logger()) {
    const auto level = spdlog::level::from_str(level_name);
    logger->set_level(level);
    logger->flush_on(spdlog::level::warn);
  }
}

spdlog::logger& logger() {
  if (!global_logger()) {
    // Late-init safety net so misuse doesn't crash; level is info.
    init_logging("info");
  }
  return *global_logger();
}

} // namespace cadly::platform

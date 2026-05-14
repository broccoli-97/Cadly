#pragma once

// Thin shim over spdlog so the rest of the codebase isn't married to a logger.
// Header keeps the SPDLOG_* macros, which give file:line info for free.

#include <spdlog/spdlog.h>

namespace cadly::platform {

// Initialise the global logger. Idempotent; safe to call multiple times.
// `level_name` accepts spdlog level names: trace, debug, info, warn, error, off.
void init_logging(const char* level_name = "info");

// Returns the global logger instance. After init_logging, all macros below
// route here.
spdlog::logger& logger();

} // namespace cadly::platform

// Convenience macros — call sites should use these so that callsite location
// metadata is preserved.
#define CADLY_LOG_TRACE(...) SPDLOG_LOGGER_TRACE(&::cadly::platform::logger(), __VA_ARGS__)
#define CADLY_LOG_DEBUG(...) SPDLOG_LOGGER_DEBUG(&::cadly::platform::logger(), __VA_ARGS__)
#define CADLY_LOG_INFO(...)  SPDLOG_LOGGER_INFO(&::cadly::platform::logger(),  __VA_ARGS__)
#define CADLY_LOG_WARN(...)  SPDLOG_LOGGER_WARN(&::cadly::platform::logger(),  __VA_ARGS__)
#define CADLY_LOG_ERROR(...) SPDLOG_LOGGER_ERROR(&::cadly::platform::logger(), __VA_ARGS__)

#pragma once

#include <cstdio>

namespace cadly::tests {
inline int checks = 0;
inline int failures = 0;

inline bool check(bool passed, const char* expression, const char* file, int line) {
  ++checks;
  if (!passed) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expression);
    ++failures;
  }
  return passed;
}

inline int report() {
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
} // namespace cadly::tests

// Unlike assert(), these checks remain active in release builds.
#define CHECK(...) ::cadly::tests::check(static_cast<bool>((__VA_ARGS__)), \
                                        #__VA_ARGS__, __FILE__, __LINE__)

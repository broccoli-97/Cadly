#pragma once

#include "cadly/cad/ICadImporter.h"

#include <chrono>
#include <mutex>

namespace cadly::cad::occt {

// OCCT exchange uses process-wide units, resource caches and algorithm
// providers. Independent body workers run inside this guard; another STEP
// or IGES reader must not change that state while they are active.
class OcctImportGuard {
public:
  explicit OcctImportGuard(IProgressSink& progress)
      : lock_(mutex(), std::defer_lock) {
    while (!progress.cancelled()) {
      if (lock_.try_lock_for(std::chrono::milliseconds(20))) break;
    }
  }

  bool acquired() const { return lock_.owns_lock(); }

private:
  static std::timed_mutex& mutex() {
    static std::timed_mutex instance;
    return instance;
  }
  std::unique_lock<std::timed_mutex> lock_;
};

} // namespace cadly::cad::occt

#pragma once

#include "cadly/cad/ICadImporter.h"

#include <memory>
#include <vector>

namespace cadly::cad {

// Owns the set of available importers. Picks one by extension/probe.
class ImporterRegistry {
public:
  static ImporterRegistry& instance();

  // Pick an importer. Returns nullptr if no plugin recognised the path.
  ICadImporter* select(const std::filesystem::path& path) const;

  // Convenience — imports `path` with default options.
  ImportResult import(const std::filesystem::path& path,
                      const ImportOptions& options = {},
                      IProgressSink* progress = nullptr) const;

  const std::vector<std::unique_ptr<ICadImporter>>& importers() const {
    return importers_;
  }

private:
  ImporterRegistry();
  std::vector<std::unique_ptr<ICadImporter>> importers_;
};

} // namespace cadly::cad

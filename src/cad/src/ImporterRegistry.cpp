#include "cadly/cad/ImporterRegistry.h"

#include "cadly/cad/OcctIgesImporter.h"
#include "cadly/cad/OcctStepImporter.h"

namespace cadly::cad {

ImporterRegistry& ImporterRegistry::instance() {
  static ImporterRegistry r;
  return r;
}

ImporterRegistry::ImporterRegistry() {
  importers_.emplace_back(std::make_unique<OcctStepImporter>());
  importers_.emplace_back(std::make_unique<OcctIgesImporter>());
}

ICadImporter* ImporterRegistry::select(const std::filesystem::path& path) const {
  for (auto& imp : importers_) {
    if (imp->CanRead(path)) return imp.get();
  }
  return nullptr;
}

ImportResult ImporterRegistry::import(const std::filesystem::path& path,
                                      const ImportOptions& options,
                                      IProgressSink* progress) const {
  NullProgressSink null_sink;
  auto& sink = progress ? *progress : null_sink;

  ICadImporter* imp = select(path);
  if (!imp) {
    ImportResult r;
    r.summary.diagnostics.push_back({DiagnosticSeverity::Error,
      "No importer recognised file extension: " + path.string()});
    return r;
  }
  ImportRequest req;
  req.path = path;
  req.options = options;
  return imp->Import(req, sink);
}

} // namespace cadly::cad

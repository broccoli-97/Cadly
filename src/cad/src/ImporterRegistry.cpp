#include "cadly/cad/ImporterRegistry.h"

#include "cadly/cad/OcctIgesImporter.h"
#include "cadly/cad/OcctStepImporter.h"

#include <Standard_Failure.hxx>

#include <exception>
#include <new>

namespace cadly::cad {

namespace {

ImportResult import_failure(const std::string& message) {
  ImportResult result;
  result.summary.diagnostics.push_back({DiagnosticSeverity::Error, message});
  return result;
}

} // namespace

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

  // Keep failures inside the CAD boundary. QtConcurrent otherwise transports
  // them to QFutureWatcher::result(), where an uncaught exception terminates
  // the GUI event loop instead of displaying the import diagnostic.
  try {
    ICadImporter* imp = select(path);
    if (!imp) {
      return import_failure("No importer recognised file extension: " + path.string());
    }
    ImportRequest req;
    req.path = path;
    req.options = options;
    return imp->Import(req, sink);
  } catch (const Standard_Failure& error) {
    const char* message = error.GetMessageString();
    return import_failure(std::string("OCCT import failed: ") +
      (message && *message ? message : "unspecified OCCT error"));
  } catch (const std::bad_alloc&) {
    return import_failure("Not enough memory to import this file.");
  } catch (const std::exception& error) {
    return import_failure(std::string("Import failed: ") + error.what());
  } catch (...) {
    return import_failure("Unknown error while importing this file.");
  }
}

} // namespace cadly::cad

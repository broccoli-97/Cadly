#include "TestChecks.h"

#include "cadly/cad/ImporterRegistry.h"
#include "cadly/cad/XcafSession.h"

#include <Standard_Failure.hxx>

#include <filesystem>
#include <new>
#include <stdexcept>
#include <string>

namespace {

enum class Failure { Occt, Standard, Memory, Unknown };

class FailingProgress final : public cadly::cad::IProgressSink {
public:
  explicit FailingProgress(Failure failure) : failure_(failure) {}

  void update(float, const std::string&) override {
    // Fail after the reader owns an XCAF document, so recovery also checks
    // that stack unwinding releases the OCCT session before the next import.
    if (cadly::cad::open_xcaf_document_count() == 0) return;
    triggered = true;
    switch (failure_) {
      case Failure::Occt: throw Standard_Failure("test OCCT failure");
      case Failure::Standard: throw std::runtime_error("test standard failure");
      case Failure::Memory: throw std::bad_alloc();
      case Failure::Unknown: throw 42;
    }
  }

  bool cancelled() const override { return false; }
  bool triggered{false};

private:
  Failure failure_;
};

void check_failure(const cadly::cad::ImportResult& result, const char* message) {
  CHECK(!result.success);
  CHECK(!result.cancelled);
  if (CHECK(!result.summary.diagnostics.empty())) {
    const auto& diagnostic = result.summary.diagnostics.front();
    CHECK(diagnostic.severity == cadly::cad::DiagnosticSeverity::Error);
    CHECK(diagnostic.message.find(message) != std::string::npos);
  }
  CHECK(cadly::cad::open_xcaf_document_count() == 0);
}

} // namespace

int main() {
  const auto fixture = std::filesystem::path(CADLY_TEST_SOURCE_ROOT) /
    "test_files/as1-ug-214.stp";
  if (!CHECK(std::filesystem::is_regular_file(fixture))) return cadly::tests::report();
  const auto& registry = cadly::cad::ImporterRegistry::instance();

  const Failure failures[] = {Failure::Occt, Failure::Standard,
                              Failure::Memory, Failure::Unknown};
  const char* messages[] = {"test OCCT failure", "test standard failure",
                            "memory", "Unknown error"};
  for (int i = 0; i < 4; ++i) {
    FailingProgress progress(failures[i]);
    const auto result = registry.import(fixture, {}, &progress);
    check_failure(result, messages[i]);
    CHECK(!result.scene);
    CHECK(progress.triggered);
    CHECK(registry.import(fixture).success);
    CHECK(cadly::cad::open_xcaf_document_count() == 0);
  }

  const auto missing = fixture.parent_path() / "cadly_import_error_missing.stp";
  if (!CHECK(!std::filesystem::exists(missing))) return cadly::tests::report();
  check_failure(registry.import(missing), "File does not exist:");

  // A long path may throw or be reported as missing, depending on the
  // platform. A normal file-not-found result may retain an empty scene.
  const auto invalid = fixture.parent_path() / (std::string(300, 'a') + ".stp");
  check_failure(registry.import(invalid), "");
  CHECK(registry.import(fixture).success);
  return cadly::tests::report();
}

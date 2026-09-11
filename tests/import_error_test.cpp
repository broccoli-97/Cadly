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
  CHECK(!result.scene);
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
    check_failure(registry.import(fixture, {}, &progress), messages[i]);
    CHECK(progress.triggered);
    CHECK(registry.import(fixture).success);
    CHECK(cadly::cad::open_xcaf_document_count() == 0);
  }

  // An invalid path must follow the same failure contract even when the
  // filesystem reports it by throwing instead of returning "not found".
  const auto invalid = fixture.parent_path() / (std::string(300, 'a') + ".stp");
  check_failure(registry.import(invalid), "");
  CHECK(registry.import(fixture).success);
  return cadly::tests::report();
}

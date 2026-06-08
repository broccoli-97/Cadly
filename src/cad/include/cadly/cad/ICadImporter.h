#pragma once

#include "cadly/scene/Scene.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace cadly::cad {

// Knobs the user can tweak in the Import Options dialog. Defaults are tuned
// for industrial assemblies in millimetres; the CLI uses the same defaults.
struct ImportOptions {
  // BRepMesh_IncrementalMesh parameters. Defaults are tuned for visualization
  // (not simulation): fine enough that cylindrical features render with
  // ~30+ facets per circle so silhouettes don't look polygonal, but not so
  // fine that import time or triangle budget blows up on large assemblies.
  // The angular budget dominates on small radii; the linear budget dominates
  // on large radii — both matter.
  double linear_deflection  {0.1};   // in model units (usually mm)
  double angular_deflection {0.35};  // radians (~20°)
  bool   relative_deflection{false};
  bool   parallel_meshing   {true};

  // Vertex output options.
  bool   compute_missing_normals{true};
  bool   weld_duplicate_vertices{false}; // per-face only; preserves seams

  // Shape healing — kept conservative; importer reports diagnostics.
  bool   run_shape_healing{true};
  double precision        {0.01};

  // Drop tiny faces below this surface area to avoid degenerate triangulation.
  double min_face_area{1e-8};

  bool   load_colors    {true};
  bool   load_names     {true};
  bool   load_hierarchy {true};

  // Developer profiling hook. Normal imports leave this off to avoid adding
  // timer overhead inside the topology walk.
  bool   profile_timings{false};
};

struct ImportRequest {
  std::filesystem::path path;
  ImportOptions         options{};
};

enum class DiagnosticSeverity { Info, Warning, Error };

struct Diagnostic {
  DiagnosticSeverity severity{DiagnosticSeverity::Info};
  std::string        message;
};

struct ImportTiming {
  std::string stage;
  std::chrono::microseconds duration{0};
};

struct ImportSummary {
  std::size_t shape_count    {0};
  std::size_t face_count     {0};
  std::size_t triangle_count {0};
  std::size_t vertex_count   {0};
  std::chrono::milliseconds total_time{0};
  std::chrono::milliseconds parse_time{0};
  std::chrono::milliseconds mesh_time {0};
  std::vector<ImportTiming> timings;
  std::vector<Diagnostic> diagnostics;
};

struct ImportResult {
  bool success{false};
  std::shared_ptr<scene::Scene> scene;
  ImportSummary summary{};
};

// Progress callback hooked into a long-running import. Importers MUST poll
// `cancelled()` between heavyweight steps so the UI can abort the job.
class IProgressSink {
public:
  virtual ~IProgressSink() = default;
  // `fraction` is in [0, 1]. `message` is a human-readable status hint.
  virtual void update(float fraction, const std::string& message) = 0;
  // Importers should check this regularly and return early when true.
  virtual bool cancelled() const = 0;
};

// Default sink: silent + never cancelled. Useful for tests and the CLI.
class NullProgressSink final : public IProgressSink {
public:
  void update(float, const std::string&) override {}
  bool cancelled() const override { return false; }
};

class ICadImporter {
public:
  virtual ~ICadImporter() = default;
  virtual bool CanRead(const std::filesystem::path& path) const = 0;
  virtual ImportResult Import(const ImportRequest& request,
                              IProgressSink& progress) = 0;
  virtual const char* Name() const = 0;
};

} // namespace cadly::cad

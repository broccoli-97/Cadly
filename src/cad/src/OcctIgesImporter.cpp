#include "cadly/cad/OcctIgesImporter.h"

#include "OcctShapeToMesh.h"

#include "cadly/platform/Log.h"

#include <IFSelect_ReturnStatus.hxx>
#include <IGESCAFControl_Reader.hxx>
#include <IGESControl_Reader.hxx>
#include <TDocStd_Application.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFApp_Application.hxx>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <string>

namespace cadly::cad {

namespace {
bool ends_with_ci(const std::string& s, const std::string& suffix) {
  if (s.size() < suffix.size()) return false;
  return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin(),
                    [](char a, char b) {
                      return std::tolower(static_cast<unsigned char>(a)) ==
                             std::tolower(static_cast<unsigned char>(b));
                    });
}
} // namespace

bool OcctIgesImporter::CanRead(const std::filesystem::path& path) const {
  const auto s = path.string();
  return ends_with_ci(s, ".igs") || ends_with_ci(s, ".iges");
}

ImportResult OcctIgesImporter::Import(const ImportRequest& req,
                                      IProgressSink& progress) {
  using clock = std::chrono::steady_clock;
  const auto t0 = clock::now();

  ImportResult result;
  result.scene = std::make_shared<scene::Scene>();
  result.scene->source_file = req.path;

  if (!std::filesystem::exists(req.path)) {
    result.summary.diagnostics.push_back({DiagnosticSeverity::Error,
      "File does not exist: " + req.path.string()});
    return result;
  }

  progress.update(0.05f, "Reading IGES file...");

  Handle(TDocStd_Application) app = XCAFApp_Application::GetApplication();
  Handle(TDocStd_Document) doc;
  app->NewDocument("MDTV-XCAF", doc);

  IGESCAFControl_Reader reader;
  reader.SetColorMode(req.options.load_colors);
  reader.SetNameMode(req.options.load_names);
  reader.SetLayerMode(true);

  const std::string path_str = req.path.string();
  const IFSelect_ReturnStatus status = reader.ReadFile(path_str.c_str());
  if (status != IFSelect_RetDone) {
    result.summary.diagnostics.push_back({DiagnosticSeverity::Error,
      "IGESCAFControl_Reader failed to read file (status=" +
        std::to_string(static_cast<int>(status)) + ")"});
    CADLY_LOG_ERROR("IGES read failed: {}", path_str);
    return result;
  }

  if (progress.cancelled()) return result;
  progress.update(0.25f, "Transferring shapes to OCAF document...");

  if (!reader.Transfer(doc)) {
    result.summary.diagnostics.push_back({DiagnosticSeverity::Error,
      "IGESCAFControl_Reader::Transfer() returned false"});
    return result;
  }
  const auto t_parse = clock::now();
  result.summary.parse_time = std::chrono::duration_cast<std::chrono::milliseconds>(
    t_parse - t0);

  progress.update(0.40f, "Tessellating geometry...");
  occt::ConversionStats stats;
  auto scn = occt::document_to_scene(doc, TopoDS_Shape{}, req.options,
                                     /*unit_to_m=*/0.001f, stats, progress);

  // Lazy geometry-only fallback (see OcctStepImporter for the full rationale):
  // the second IGESControl_Reader parse only runs when the XDE walk produced
  // no nodes, instead of unconditionally re-parsing the whole file every time.
  if (!scn || scn->nodes.empty()) {
    IGESControl_Reader bare;
    if (bare.ReadFile(path_str.c_str()) == IFSelect_RetDone) {
      bare.TransferRoots();
      if (bare.NbShapes() > 0) {
        scn = occt::document_to_scene(Handle(TDocStd_Document){}, bare.OneShape(),
                                      req.options, /*unit_to_m=*/0.001f, stats,
                                      progress);
      }
    }
  }
  result.scene = std::move(scn);
  if (result.scene) {
    result.scene->source_file = req.path;
  }

  const auto t_mesh = clock::now();
  result.summary.mesh_time = std::chrono::duration_cast<std::chrono::milliseconds>(
    t_mesh - t_parse);
  result.summary.total_time = std::chrono::duration_cast<std::chrono::milliseconds>(
    t_mesh - t0);
  result.summary.face_count     = stats.face_count;
  result.summary.triangle_count = stats.triangle_count;
  result.summary.vertex_count   = stats.vertex_count;
  result.summary.shape_count    = result.scene ? result.scene->nodes.size() : 0;
  for (auto& d : stats.diagnostics)
    result.summary.diagnostics.push_back(std::move(d));

  if (!result.scene || result.scene->nodes.empty()) {
    result.summary.diagnostics.push_back({DiagnosticSeverity::Warning,
      "Document produced no scene nodes."});
    return result;
  }

  progress.update(1.0f, "Import complete.");
  result.success = true;
  return result;
}

} // namespace cadly::cad

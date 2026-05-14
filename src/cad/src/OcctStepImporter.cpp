#include "cadly/cad/OcctStepImporter.h"

#include "OcctShapeToMesh.h"

#include "cadly/platform/Log.h"

#include <IFSelect_ReturnStatus.hxx>
#include <Interface_Static.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <STEPControl_Reader.hxx>
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

float unit_to_meters_from_step() {
  // STEPCAFControl_Reader exposes the unit via Interface_Static. After
  // ReadFile + Transfer this value is the document unit length name.
  Standard_CString unit_name = Interface_Static::CVal("xstep.cascade.unit");
  if (!unit_name) return 0.001f; // assume mm
  std::string u = unit_name;
  std::transform(u.begin(), u.end(), u.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  if (u == "m" || u == "meter")          return 1.0f;
  if (u == "cm" || u == "centimeter")    return 0.01f;
  if (u == "mm" || u == "millimeter")    return 0.001f;
  if (u == "in" || u == "inch")          return 0.0254f;
  if (u == "ft" || u == "foot")          return 0.3048f;
  return 0.001f;
}

} // namespace

bool OcctStepImporter::CanRead(const std::filesystem::path& path) const {
  const auto s = path.string();
  return ends_with_ci(s, ".step") || ends_with_ci(s, ".stp") ||
         ends_with_ci(s, ".p21");
}

ImportResult OcctStepImporter::Import(const ImportRequest& req,
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

  progress.update(0.05f, "Reading STEP file...");

  // The CAF reader needs an application/document host.
  Handle(TDocStd_Application) app = XCAFApp_Application::GetApplication();
  Handle(TDocStd_Document) doc;
  app->NewDocument("MDTV-XCAF", doc);

  STEPCAFControl_Reader reader;
  reader.SetColorMode(req.options.load_colors);
  reader.SetNameMode(req.options.load_names);
  reader.SetLayerMode(true);
  reader.SetMatMode(true);

  const std::string path_str = req.path.string();
  const IFSelect_ReturnStatus status = reader.ReadFile(path_str.c_str());
  if (status != IFSelect_RetDone) {
    result.summary.diagnostics.push_back({DiagnosticSeverity::Error,
      "STEPCAFControl_Reader failed to read file (status=" +
        std::to_string(static_cast<int>(status)) + ")"});
    CADLY_LOG_ERROR("STEP read failed: {}", path_str);
    return result;
  }

  if (progress.cancelled()) return result;
  progress.update(0.25f, "Transferring shapes to OCAF document...");

  if (!reader.Transfer(doc)) {
    result.summary.diagnostics.push_back({DiagnosticSeverity::Error,
      "STEPCAFControl_Reader::Transfer() returned false"});
    return result;
  }
  const auto t_parse = clock::now();
  result.summary.parse_time = std::chrono::duration_cast<std::chrono::milliseconds>(t_parse - t0);

  const float unit_to_m = unit_to_meters_from_step();
  result.scene->source_unit    = "mm"; // best-effort; OCCT scales to mm internally
  result.scene->unit_to_meters = unit_to_m;

  progress.update(0.40f, "Tessellating geometry...");
  occt::ConversionStats stats;
  // We also keep a fallback (full one-shot) shape in case the doc walk
  // produces nothing — STEPControl_Reader is used here for a stitched shape.
  TopoDS_Shape fallback;
  {
    STEPControl_Reader bare;
    if (bare.ReadFile(path_str.c_str()) == IFSelect_RetDone) {
      bare.TransferRoots();
      if (bare.NbShapes() > 0) {
        fallback = bare.OneShape();
      }
    }
  }

  auto scn = occt::document_to_scene(doc, fallback, req.options, unit_to_m,
                                     stats, progress);
  // Carry diagnostics + summary up.
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

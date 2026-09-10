#include "TestChecks.h"

#include "cadly/cad/ImporterRegistry.h"
#include "cadly/cad/XcafSession.h"
#include "OcctImportGuard.h"
#include "OcctShapeToMesh.h"
#include "OcctStepTransfer.h"
#include "XcafDocumentLease.h"

#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Builder.hxx>
#include <IGESCAFControl_Writer.hxx>
#include <Interface_Static.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <Standard_Version.hxx>
#include <StepBasic_SiUnitAndLengthUnit.hxx>
#include <StepData_StepModel.hxx>
#include <StepGeom_GeomRepContextAndGlobUnitAssCtxAndGlobUncertaintyAssCtx.hxx>
#include <StepShape_ManifoldSolidBrep.hxx>
#include <StepShape_ShapeRepresentation.hxx>
#include <TDF_ChildIterator.hxx>
#include <TDataStd_Name.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Compound.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XSControl_WorkSession.hxx>
#include <gp_Pln.hxx>
#if OCC_VERSION_HEX < 0x070900
#include <XSAlgo.hxx>
#include <XSAlgo_AlgoContainer.hxx>
#endif

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

using namespace cadly;

namespace {

struct TemporaryFiles {
  std::filesystem::path path;
  TemporaryFiles() {
    path = std::filesystem::temp_directory_path() / ("cadly-import-test-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(path);
  }
  ~TemporaryFiles() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

bool close(float a, float b) { return std::abs(a - b) < 1e-5f; }
bool close(const scene::vec3& a, const scene::vec3& b) {
  return close(a.x, b.x) && close(a.y, b.y) && close(a.z, b.z);
}

void compare_scenes(const scene::Scene& a, const scene::Scene& b) {
  CHECK(close(a.world_bounds.min, b.world_bounds.min));
  CHECK(close(a.world_bounds.max, b.world_bounds.max));
  CHECK(a.source_unit == b.source_unit);
  CHECK(a.unit_to_meters == b.unit_to_meters);
  if (!CHECK(a.nodes.size() == b.nodes.size()) ||
      !CHECK(a.meshes.size() == b.meshes.size()) ||
      !CHECK(a.materials.size() == b.materials.size())) return;
  for (std::size_t i = 0; i < a.nodes.size(); ++i) {
    const auto& x = a.nodes[i];
    const auto& y = b.nodes[i];
    CHECK(x.name == y.name);
    CHECK(x.source_label == y.source_label);
    CHECK(x.parent == y.parent);
    CHECK(x.children == y.children);
    CHECK(x.mesh_index == y.mesh_index);
    CHECK(x.material_override == y.material_override);
    for (int c = 0; c < 4; ++c)
      for (int r = 0; r < 4; ++r)
        CHECK(close(x.world_matrix[c][r], y.world_matrix[c][r]));
  }
  for (std::size_t i = 0; i < a.meshes.size(); ++i) {
    const auto& x = *a.meshes[i];
    const auto& y = *b.meshes[i];
    CHECK(x.name == y.name);
    CHECK(x.indices == y.indices);
    CHECK(x.edge_strip_indices == y.edge_strip_indices);
    CHECK(x.double_sided == y.double_sided);
    if (CHECK(x.vertices.size() == y.vertices.size())) {
      for (std::size_t v = 0; v < x.vertices.size(); ++v) {
        CHECK(close(x.vertices[v].position, y.vertices[v].position));
        CHECK(close(x.vertices[v].normal, y.vertices[v].normal));
        CHECK(x.vertices[v].color_rgba8 == y.vertices[v].color_rgba8);
      }
    }
    if (CHECK(x.submeshes.size() == y.submeshes.size())) {
      for (std::size_t s = 0; s < x.submeshes.size(); ++s) {
        CHECK(x.submeshes[s].index_offset == y.submeshes[s].index_offset);
        CHECK(x.submeshes[s].index_count == y.submeshes[s].index_count);
        CHECK(x.submeshes[s].source_face_id == y.submeshes[s].source_face_id);
        CHECK(x.submeshes[s].material_index == y.submeshes[s].material_index);
      }
    }
    if (CHECK(x.edge_lods.size() == y.edge_lods.size())) {
      for (std::size_t l = 0; l < x.edge_lods.size(); ++l) {
        CHECK(x.edge_lods[l].indices == y.edge_lods[l].indices);
        CHECK(close(x.edge_lods[l].linear_deflection, y.edge_lods[l].linear_deflection));
        if (CHECK(x.edge_lods[l].vertices.size() == y.edge_lods[l].vertices.size()))
          for (std::size_t v = 0; v < x.edge_lods[l].vertices.size(); ++v)
            CHECK(close(x.edge_lods[l].vertices[v], y.edge_lods[l].vertices[v]));
      }
    }
  }
  for (std::size_t i = 0; i < a.materials.size(); ++i) {
    CHECK(a.materials[i].base_color == b.materials[i].base_color);
    CHECK(a.materials[i].roughness == b.materials[i].roughness);
    CHECK(a.materials[i].metallic == b.materials[i].metallic);
  }
}

std::size_t label_count(const TDF_Label& root) {
  std::size_t count = 1;
  for (TDF_ChildIterator it(root, Standard_True); it.More(); it.Next()) ++count;
  return count;
}

TopoDS_Shape make_assembly(const Handle(TDocStd_Document)& doc) {
  const auto shapes = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
  const auto colors = XCAFDoc_DocumentTool::ColorTool(doc->Main());
  const auto box = BRepPrimAPI_MakeBox(10, 20, 30).Shape();
  const auto pin = BRepPrimAPI_MakeCylinder(3, 5).Shape();
  const auto box_label = shapes->AddShape(box, false);
  TDataStd_Name::Set(box_label, "bracket");
  colors->SetColor(box_label, Quantity_Color(0, 0, 1, Quantity_TOC_RGB), XCAFDoc_ColorGen);
  TopExp_Explorer face(box, TopAbs_FACE);
  const auto face_label = shapes->AddSubShape(box_label, face.Current());
  colors->SetColor(face_label, Quantity_Color(1, 0, 0, Quantity_TOC_RGB), XCAFDoc_ColorSurf);
  const auto pin_label = shapes->AddShape(pin, false);
  TDataStd_Name::Set(pin_label, "pin");

  const auto assembly = shapes->NewShape();
  TDataStd_Name::Set(assembly, "two brackets and a pin");
  const auto first = shapes->AddComponent(assembly, box_label, TopLoc_Location());
  TDataStd_Name::Set(first, "first");
  colors->SetColor(first, Quantity_Color(1, 1, 0, Quantity_TOC_RGB), XCAFDoc_ColorGen);
  gp_Trsf location;
  location.SetTranslation(gp_Vec(40, 0, 0));
  const auto second = shapes->AddComponent(assembly, box_label, TopLoc_Location(location));
  TDataStd_Name::Set(second, "second");
  location.SetTranslation(gp_Vec(0, 40, 0));
  const auto third = shapes->AddComponent(assembly, pin_label, TopLoc_Location(location));
  TDataStd_Name::Set(third, "third");
  shapes->UpdateAssemblies();
  return shapes->GetShape(assembly);
}

void test_colors_and_instancing(const std::filesystem::path& directory) {
  cad::NullProgressSink progress;
  cad::occt::OcctImportGuard guard(progress);
  cad::occt::XcafDocumentLease lease;
  make_assembly(lease.doc());
  const auto labels_before = label_count(lease.doc()->Main());
  cad::ImportOptions options;
  cad::occt::ConversionStats stats;
  const auto scn = cad::occt::document_to_scene(lease.doc(), {}, options, 0.001f, stats, progress);
  CHECK(label_count(lease.doc()->Main()) == labels_before);
  CHECK(scn->nodes.size() == 4);
  CHECK(scn->meshes.size() == 2);
  CHECK(scn->materials.size() == 4);
  // Circular mesh extrema can fall between sampled vertices.
  CHECK(glm::length(scn->world_bounds.min - scene::vec3(-3, 0, 0)) <= stats.resolved_linear_deflection);
  CHECK(glm::length(scn->world_bounds.max - scene::vec3(50, 43, 30)) <= stats.resolved_linear_deflection);
  const scene::Node* first = nullptr;
  const scene::Node* second = nullptr;
  for (const auto& node : scn->nodes) {
    if (node.name == "first") first = &node;
    if (node.name == "second") second = &node;
  }
  if (CHECK(first && second)) {
    CHECK(first->mesh_index == second->mesh_index);
    CHECK(first->material_override.has_value());
    CHECK(!second->material_override.has_value());
    const auto& mesh = *scn->meshes[*first->mesh_index];
    CHECK(mesh.submeshes.size() == 6);
    for (const auto& sub : mesh.submeshes) {
      const auto expected = sub.source_face_id == 0 ? scene::vec4(1, 0, 0, 1) :
                                                                    scene::vec4(0, 0, 1, 1);
      CHECK(scn->materials[sub.material_index].base_color == expected);
    }
    for (const auto& vertex : mesh.vertices) CHECK(vertex.color_rgba8 == 0xFFFFFFFFu);
  }

  options.load_colors = false;
  cad::occt::ConversionStats uncolored_stats;
  const auto uncolored = cad::occt::document_to_scene(lease.doc(), {}, options, 0.001f,
                                                    uncolored_stats, progress);
  CHECK(uncolored->materials.size() == 1);
  for (const auto& node : uncolored->nodes) CHECK(!node.material_override);
  for (const auto& mesh : uncolored->meshes)
    for (const auto& sub : mesh->submeshes) CHECK(sub.material_index == 0);
  CHECK(label_count(lease.doc()->Main()) == labels_before);

  STEPCAFControl_Writer writer;
  CHECK(writer.Transfer(lease.doc(), STEPControl_AsIs));
  CHECK(writer.Write((directory / "assembly.step").string().c_str()) == IFSelect_RetDone);
  IGESCAFControl_Writer iges;
  CHECK(iges.Perform(lease.doc(), (directory / "assembly.iges").string().c_str()));
}

void test_skipped_face_color() {
  cad::NullProgressSink progress;
  cad::occt::OcctImportGuard guard(progress);
  cad::occt::XcafDocumentLease lease;
  const auto shapes = XCAFDoc_DocumentTool::ShapeTool(lease.doc()->Main());
  const auto colors = XCAFDoc_DocumentTool::ColorTool(lease.doc()->Main());
  BRep_Builder builder;
  TopoDS_Compound compound;
  builder.MakeCompound(compound);
  const auto tiny = BRepBuilderAPI_MakeFace(gp_Pln(gp::XOY()), 0, 0.001, 0, 0.001).Shape();
  const auto large = BRepBuilderAPI_MakeFace(gp_Pln(gp::XOY()), 1, 4, 1, 4).Shape();
  builder.Add(compound, tiny);
  builder.Add(compound, large);
  const auto label = shapes->AddShape(compound, false);
  colors->SetColor(shapes->AddSubShape(label, tiny), Quantity_NOC_RED, XCAFDoc_ColorSurf);
  colors->SetColor(shapes->AddSubShape(label, large), Quantity_NOC_GREEN, XCAFDoc_ColorSurf);
  cad::ImportOptions options;
  options.min_face_area = 1e-5;
  cad::occt::ConversionStats stats;
  const auto scn = cad::occt::document_to_scene(lease.doc(), {}, options, 0.001f, stats, progress);
  if (CHECK(scn->meshes.size() == 1)) {
    const auto& mesh = *scn->meshes.front();
    if (CHECK(mesh.submeshes.size() == 1)) {
      CHECK(mesh.submeshes.front().source_face_id == 1);
      CHECK(scn->materials[mesh.submeshes.front().material_index].base_color ==
            scene::vec4(0, 1, 0, 1));
    }
  }
}

void test_step_buffer_boundaries(const std::filesystem::path& directory) {
  std::ifstream input(directory / "assembly.step", std::ios::binary);
  std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  if (!CHECK(!text.empty())) return;
  // Cross input refill boundaries while staying below OCCT 7.6's 16 KiB
  // individual-token limit. Doubled quotes must become single apostrophes.
  const std::string name = std::string(4090, 'a') + " 'bracket' " + std::string(9000, 'b');
  std::string quoted = "'";
  for (const char c : name) {
    quoted += c;
    if (c == '\'') quoted += c;
  }
  quoted += '\'';
  const std::string original = "'bracket'";
  bool replaced = false;
  for (auto at = text.find(original); at != std::string::npos; at = text.find(original, at + quoted.size())) {
    text.replace(at, original.size(), quoted);
    replaced = true;
  }
  if (!CHECK(replaced)) return;
  const auto data = text.find("DATA;");
  if (!CHECK(data != std::string::npos)) return;
  std::string comment = "/*";
  for (int i = 0; i < 8; ++i) comment += std::string(9000, 'c') + '\n';
  text.insert(data, comment + " 'ignored' #42=(1,2,3); */\n");
  const auto path = directory / "long-tokens.step";
  {
    std::ofstream output(path, std::ios::binary);
    output << text;
  }
  const auto result = cad::ImporterRegistry::instance().import(path, cad::ImportOptions{});
  if (!CHECK(result.success && result.scene)) return;
  CHECK(result.scene->nodes.size() == 4);
  CHECK(result.scene->meshes.size() == 2);
  bool found_name = false;
  for (const auto& mesh : result.scene->meshes) {
    if (mesh->name == name) found_name = true;
  }
  CHECK(found_name);
}

enum class Mutation { SharedTopology, MixedUnits, AmbiguousBody };

cad::ImportResult mutated_import(const std::filesystem::path& path, Mutation mutation,
                                bool parallel) {
  cad::NullProgressSink progress;
  cad::occt::OcctImportGuard guard(progress);
  cad::occt::XcafDocumentLease lease;
  STEPCAFControl_Reader reader;
  cad::ImportResult result;
  if (!CHECK(reader.ReadFile(path.string().c_str()) == IFSelect_RetDone)) return result;
  const auto model = reader.Reader().StepModel();
  std::vector<Handle(StepShape_ManifoldSolidBrep)> bodies;
  std::vector<Handle(StepShape_ShapeRepresentation)> reps;
  for (int i = 1; i <= model->NbEntities(); ++i) {
    const auto rep = Handle(StepShape_ShapeRepresentation)::DownCast(model->Value(i));
    if (rep.IsNull()) continue;
    for (int j = 1; j <= rep->NbItems(); ++j) {
      const auto body = Handle(StepShape_ManifoldSolidBrep)::DownCast(rep->ItemsValue(j));
      if (!body.IsNull()) { bodies.push_back(body); reps.push_back(rep); }
    }
  }
  if (!CHECK(bodies.size() == 2)) return result;
  if (mutation == Mutation::SharedTopology) {
    bodies[1]->SetOuter(bodies[0]->Outer());
  } else {
    const auto original = Handle(StepGeom_GeomRepContextAndGlobUnitAssCtxAndGlobUncertaintyAssCtx)
      ::DownCast(reps[1]->ContextOfItems());
    if (!CHECK(!original.IsNull())) return result;
    using UnitArray = std::remove_reference_t<decltype(*original->Units())>;
    Handle(UnitArray) units = new UnitArray(1, original->NbUnits());
    for (int i = 1; i <= original->NbUnits(); ++i) units->SetValue(i, original->UnitsValue(i));
    if (mutation == Mutation::MixedUnits) {
      Handle(StepBasic_SiUnitAndLengthUnit) centimeters = new StepBasic_SiUnitAndLengthUnit;
      centimeters->Init(true, StepBasic_spCenti, StepBasic_sunMetre);
      for (int i = 1; i <= units->Length(); ++i)
        if (!Handle(StepBasic_SiUnitAndLengthUnit)::DownCast(units->Value(i)).IsNull())
          units->SetValue(i, centimeters);
    } else {
      for (int i = 1; i <= reps[1]->NbItems(); ++i)
        if (reps[1]->ItemsValue(i) == bodies[1]) reps[1]->Items()->SetValue(i, bodies[0]);
    }
    Handle(StepGeom_GeomRepContextAndGlobUnitAssCtxAndGlobUncertaintyAssCtx) context =
      new StepGeom_GeomRepContextAndGlobUnitAssCtxAndGlobUncertaintyAssCtx;
    context->Init(original->ContextIdentifier(), original->ContextType(),
      original->CoordinateSpaceDimension(), units, original->Uncertainty());
    reps[1]->SetContextOfItems(context);
    model->AddWithRefs(context);
  }
  CHECK(reader.ChangeReader().WS()->ComputeGraph(true));
  cad::ImportOptions options;
  options.parallel_step_transfer = parallel;
  options.step_transfer_threads = 2;
  result.success = cad::occt::transfer_step(reader, lease.doc(), options, result.summary, progress);
  cad::occt::ConversionStats stats;
  result.scene = cad::occt::document_to_scene(lease.doc(), {}, options, 0.001f, stats, progress);
  return result;
}

class CancelSink final : public cad::IProgressSink {
public:
  std::atomic<bool> cancel{false};
  bool during_transfer{false};
  bool saw_transfer{false};
  void update(float fraction, const std::string& message) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (fraction < last_ - 1e-5f) monotone = false;
    last_ = fraction;
    if (message == "Converting STEP bodies..." && fraction > 0.28f && fraction < 0.56f) {
      saw_transfer = true;
      if (during_transfer) cancel = true;
    }
  }
  bool cancelled() const override { return cancel.load(); }
  bool monotone{true};
private:
  std::mutex mutex_;
  float last_{0};
};

void test_transfer_and_cancel(const std::filesystem::path& directory) {
  const auto file = directory / "assembly.step";
  const auto& registry = cad::ImporterRegistry::instance();
#if OCC_VERSION_HEX < 0x070900
  const auto provider_before = XSAlgo::AlgoContainer();
#endif
  const auto documents_before = cad::open_xcaf_document_count();
  cad::ImportOptions options;
  CHECK(options.step_healing_mode == cad::StepHealingMode::Full);
  options.step_transfer_threads = 1;
  const auto serial = registry.import(file, options);
  CHECK(serial.success);
  CHECK(serial.summary.step_transfer_threads == 1);
  options.step_transfer_threads = 2;
  CancelSink progress;
  const auto parallel = registry.import(file, options, &progress);
  CHECK(parallel.success);
  CHECK(parallel.summary.step_body_count == 2);
  CHECK(parallel.summary.step_transfer_threads == 2);
  CHECK(progress.monotone);
  CHECK(progress.saw_transfer);
  if (serial.success && parallel.success) compare_scenes(*serial.scene, *parallel.scene);

  options.step_healing_mode = cad::StepHealingMode::Fast;
  const auto fast = registry.import(file, options);
  CHECK(fast.success);
  CHECK(fast.summary.step_healing_mode == cad::StepHealingMode::Fast);
  if (serial.success && fast.success) compare_scenes(*serial.scene, *fast.scene);
  options.step_transfer_threads = std::numeric_limits<unsigned>::max();
  const auto bounded = registry.import(file, options);
  CHECK(bounded.success);
  CHECK(bounded.summary.step_transfer_threads <= 64);

  for (const auto mutation : {Mutation::SharedTopology, Mutation::MixedUnits, Mutation::AmbiguousBody}) {
    const auto a = mutated_import(file, mutation, false);
    const auto b = mutated_import(file, mutation, true);
    CHECK(a.success && b.success);
    const unsigned expected = mutation == Mutation::MixedUnits && OCC_VERSION_HEX >= 0x070800 ? 2 : 1;
    CHECK(b.summary.step_transfer_threads == expected);
    if (a.success && b.success) compare_scenes(*a.scene, *b.scene);
  }

  options = cad::ImportOptions{};
  options.step_transfer_threads = 2;
  CancelSink cancel;
  cancel.during_transfer = true;
  const auto cancelled = registry.import(file, options, &cancel);
  CHECK(cancel.saw_transfer);
  CHECK(cancelled.cancelled && !cancelled.success);
  const auto after_cancel = registry.import(file, options);
  CHECK(after_cancel.success);
  if (serial.success && after_cancel.success) compare_scenes(*serial.scene, *after_cancel.scene);

  CancelSink already_cancelled;
  already_cancelled.cancel = true;
  const auto unopened = registry.import(file, options, &already_cancelled);
  CHECK(unopened.cancelled && !unopened.success);
  CHECK(unopened.summary.step_entity_count == 0);

  // A queued reader must be cancellable without touching global OCCT state.
  {
    cad::NullProgressSink holder;
    cad::occt::OcctImportGuard guard(holder);
    CancelSink waiting;
    auto pending = std::async(std::launch::async, [&] { return registry.import(file, options, &waiting); });
    CHECK(pending.wait_for(std::chrono::milliseconds(40)) == std::future_status::timeout);
    waiting.cancel = true;
    CHECK(pending.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    const auto result = pending.get();
    CHECK(result.cancelled && !result.success);
  }
  const auto iges = registry.import(directory / "assembly.iges", options);
  CHECK(iges.success);
  CHECK(iges.scene && !iges.scene->meshes.empty());
  CHECK(cad::open_xcaf_document_count() == documents_before);
#if OCC_VERSION_HEX < 0x070900
  CHECK(XSAlgo::AlgoContainer() == provider_before);
#endif
}

} // namespace

int main() {
  TemporaryFiles files;
  test_colors_and_instancing(files.path);
  test_skipped_face_color();
  test_step_buffer_boundaries(files.path);
  test_transfer_and_cancel(files.path);
  return tests::report();
}

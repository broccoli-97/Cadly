#include "OcctShapeToMesh.h"

#include "cadly/platform/Log.h"
#include "cadly/scene/Aabb.h"
#include "cadly/scene/Math.h"
#include "cadly/scene/Node.h"
#include "cadly/scene/Scene.h"

#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <GProp_GProps.hxx>
#include <Poly_PolygonOnTriangulation.hxx>
#include <Poly_Triangle.hxx>
#include <Poly_Triangulation.hxx>
#include <Standard_Version.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace cadly::cad::occt {

namespace {

std::uint32_t pack_color(const Quantity_Color& c, float alpha = 1.0f) {
  auto clamp01 = [](double v) {
    return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
  };
  const auto r = static_cast<std::uint32_t>(clamp01(c.Red())   * 255.0 + 0.5);
  const auto g = static_cast<std::uint32_t>(clamp01(c.Green()) * 255.0 + 0.5);
  const auto b = static_cast<std::uint32_t>(clamp01(c.Blue())  * 255.0 + 0.5);
  const auto a = static_cast<std::uint32_t>(clamp01(alpha)     * 255.0 + 0.5);
  return r | (g << 8) | (b << 16) | (a << 24);
}

// Tessellate a shape in place. OCCT mutates the BRep with per-face Poly_*.
void tessellate(const TopoDS_Shape& shape, const ImportOptions& opts) {
  BRepMesh_IncrementalMesh mesher(shape,
                                  opts.linear_deflection,
                                  opts.relative_deflection,
                                  opts.angular_deflection,
                                  opts.parallel_meshing);
  mesher.Perform();
}

void add_timing(ConversionStats& stats,
                const char* stage,
                std::chrono::steady_clock::duration duration) {
  if (!duration.count()) return;
  const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
    duration);
  for (auto& timing : stats.timings) {
    if (timing.stage == stage) {
      timing.duration += elapsed;
      return;
    }
  }
  stats.timings.push_back({stage, elapsed});
}

// Push one face's triangulation into the running Mesh buffers. Returns the
// number of triangles emitted. `seen_edges` is shared across all faces of a
// shape and tracks which TopoDS_Edges have already had their mesh-coupled
// polyline emitted — an edge bordering two faces would otherwise be drawn
// twice from each face's own copy of the boundary nodes.
std::size_t append_face(scene::Mesh& mesh,
                        const TopoDS_Face& face,
                        const std::optional<Quantity_Color>& face_color,
                        const ImportOptions& opts,
                        ConversionStats& stats,
                        std::uint32_t source_face_id,
                        std::unordered_set<const void*>& seen_edges) {
  using clock = std::chrono::steady_clock;
  TopLoc_Location loc;
  Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
  if (tri.IsNull()) return 0;

  if (opts.min_face_area > 0.0) {
    const auto phase_start = clock::now();
    GProp_GProps props;
    BRepGProp::SurfaceProperties(face, props);
    if (opts.profile_timings) {
      add_timing(stats, "face area checks", clock::now() - phase_start);
    }
    if (props.Mass() < opts.min_face_area) return 0;
  }

  const gp_Trsf& trsf = loc.Transformation();
  const bool reversed = (face.Orientation() == TopAbs_REVERSED);

  // Submesh slot allocated up-front; index_offset filled now, count later.
  scene::Submesh sub;
  sub.material_index   = 0;   // default; importer can later override
  sub.source_face_id   = source_face_id;
  sub.index_offset     = static_cast<std::uint32_t>(mesh.indices.size());
  sub.bounds           = scene::Aabb::empty();

  const std::uint32_t base_vertex = static_cast<std::uint32_t>(mesh.vertices.size());
  const std::uint32_t color_packed = face_color
    ? pack_color(*face_color)
    : 0xFFFFFFFFu;

#if OCC_VERSION_HEX >= 0x070600
  const Standard_Integer node_count = tri->NbNodes();
  auto phase_start = clock::now();
  for (Standard_Integer i = 1; i <= node_count; ++i) {
    gp_Pnt p = tri->Node(i);
    if (!loc.IsIdentity()) p.Transform(trsf);
    scene::Vertex v{};
    v.position = scene::vec3(static_cast<float>(p.X()),
                             static_cast<float>(p.Y()),
                             static_cast<float>(p.Z()));
    v.color_rgba8 = color_packed;
    if (tri->HasNormals()) {
      gp_Dir n = tri->Normal(i);
      if (!loc.IsIdentity()) n.Transform(trsf);
      if (reversed) n.Reverse();
      v.normal = scene::vec3(static_cast<float>(n.X()),
                             static_cast<float>(n.Y()),
                             static_cast<float>(n.Z()));
    }
    sub.bounds.expand(v.position);
    mesh.vertices.push_back(v);
  }
  if (opts.profile_timings) {
    add_timing(stats, "face vertex extraction", clock::now() - phase_start);
  }
#else
  const auto& nodes = tri->Nodes();
  const Standard_Integer node_count = nodes.Length();
  auto phase_start = clock::now();
  for (Standard_Integer i = 1; i <= node_count; ++i) {
    gp_Pnt p = nodes(i);
    if (!loc.IsIdentity()) p.Transform(trsf);
    scene::Vertex v{};
    v.position = scene::vec3(static_cast<float>(p.X()),
                             static_cast<float>(p.Y()),
                             static_cast<float>(p.Z()));
    v.color_rgba8 = color_packed;
    sub.bounds.expand(v.position);
    mesh.vertices.push_back(v);
  }
  if (opts.profile_timings) {
    add_timing(stats, "face vertex extraction", clock::now() - phase_start);
  }
#endif

  const Standard_Integer tri_count = tri->NbTriangles();
  std::size_t emitted = 0;
  phase_start = clock::now();
  for (Standard_Integer i = 1; i <= tri_count; ++i) {
    Standard_Integer a = 0, b = 0, c = 0;
#if OCC_VERSION_HEX >= 0x070600
    const Poly_Triangle& t = tri->Triangle(i);
#else
    const Poly_Triangle& t = tri->Triangles().Value(i);
#endif
    t.Get(a, b, c);
    // Convert to 0-based and apply face orientation.
    const std::uint32_t i0 = base_vertex + static_cast<std::uint32_t>(a - 1);
    const std::uint32_t i1 = base_vertex + static_cast<std::uint32_t>(b - 1);
    const std::uint32_t i2 = base_vertex + static_cast<std::uint32_t>(c - 1);
    if (reversed) {
      mesh.indices.push_back(i0);
      mesh.indices.push_back(i2);
      mesh.indices.push_back(i1);
    } else {
      mesh.indices.push_back(i0);
      mesh.indices.push_back(i1);
      mesh.indices.push_back(i2);
    }
    ++emitted;
  }
  if (opts.profile_timings) {
    add_timing(stats, "triangle index extraction", clock::now() - phase_start);
  }

  // If OCCT didn't give us normals, derive flat normals per triangle.
  if (!tri->HasNormals() && opts.compute_missing_normals) {
    phase_start = clock::now();
    for (std::uint32_t i = base_vertex; i < mesh.vertices.size(); ++i) {
      mesh.vertices[i].normal = scene::vec3(0.0f);
    }
    for (std::uint32_t i = sub.index_offset;
         i + 2 < sub.index_offset + emitted * 3;
         i += 3) {
      auto& v0 = mesh.vertices[mesh.indices[i + 0]];
      auto& v1 = mesh.vertices[mesh.indices[i + 1]];
      auto& v2 = mesh.vertices[mesh.indices[i + 2]];
      const scene::vec3 e1 = v1.position - v0.position;
      const scene::vec3 e2 = v2.position - v0.position;
      scene::vec3 n = glm::cross(e1, e2);
      const float len2 = glm::dot(n, n);
      if (len2 > 1e-12f) n /= std::sqrt(len2);
      v0.normal += n; v1.normal += n; v2.normal += n;
    }
    for (std::uint32_t i = base_vertex; i < mesh.vertices.size(); ++i) {
      const float len2 = glm::dot(mesh.vertices[i].normal,
                                  mesh.vertices[i].normal);
      if (len2 > 1e-12f) {
        mesh.vertices[i].normal /= std::sqrt(len2);
      } else {
        mesh.vertices[i].normal = scene::vec3(0.0f, 1.0f, 0.0f);
      }
    }
    if (opts.profile_timings) {
      add_timing(stats, "normal generation", clock::now() - phase_start);
    }
  }

  sub.index_count = static_cast<std::uint32_t>(emitted * 3);
  mesh.submeshes.push_back(sub);
  mesh.bounds.expand(sub.bounds);

  // Mesh-coupled BRep edge polylines. Each TopoDS_Edge of the face carries
  // a Poly_PolygonOnTriangulation: a 1-based array of integers indexing
  // into THIS face's node array. Emit one GL_LINE_STRIP per edge,
  // terminated by the 0xFFFFFFFF primitive-restart sentinel, with indices
  // pointing into the global `mesh.vertices` (offset by `base_vertex`).
  // The resulting edge vertices are literally the same memory as the
  // corresponding face vertices, so the "shaded with edges" overlay needs
  // only glPolygonOffset to keep edges visible: their depth values are
  // identical before offset.
  //
  // Strip + restart instead of GL_LINES pairs: the old layout repeated each
  // interior node in two consecutive segments, and draw_edges' alpha-blended
  // ink rasterised the joint pixel twice, leaving a row of dark "dots" along
  // every curved edge. Strips render each joint once and the dots disappear.
  //
  // Dedup is keyed on TShape*. An edge shared by two faces produces a
  // valid PolygonOnTriangulation under each face's triangulation, but
  // OCCT seeds both with the same boundary sample points so it does not
  // matter which face we use — picking the first one we see is correct.
  phase_start = clock::now();
  for (TopExp_Explorer ex_e(face, TopAbs_EDGE); ex_e.More(); ex_e.Next()) {
    const TopoDS_Edge& edge = TopoDS::Edge(ex_e.Current());
    if (BRep_Tool::Degenerated(edge)) continue;
    const void* key = edge.TShape().get();
    if (!seen_edges.insert(key).second) continue;

    Handle(Poly_PolygonOnTriangulation) poly =
      BRep_Tool::PolygonOnTriangulation(edge, tri, loc);
    if (poly.IsNull()) continue;
    const TColStd_Array1OfInteger& nodes = poly->Nodes();
    if (nodes.Length() < 2) continue;
    for (Standard_Integer i = nodes.Lower(); i <= nodes.Upper(); ++i) {
      mesh.edge_strip_indices.push_back(
        base_vertex + static_cast<std::uint32_t>(nodes.Value(i) - 1));
    }
    mesh.edge_strip_indices.push_back(0xFFFFFFFFu);
  }
  if (opts.profile_timings) {
    add_timing(stats, "mesh-coupled edge strips", clock::now() - phase_start);
  }

  stats.triangle_count += emitted;
  stats.vertex_count   += node_count;
  ++stats.face_count;
  return emitted;
}

std::optional<Quantity_Color>
resolve_face_color(const Handle(XCAFDoc_ColorTool)& color_tool,
                   const TopoDS_Shape& face) {
  if (color_tool.IsNull()) return std::nullopt;
  Quantity_Color c;
  if (color_tool->GetColor(face, XCAFDoc_ColorSurf, c)) return c;
  if (color_tool->GetColor(face, XCAFDoc_ColorGen,  c)) return c;
  return std::nullopt;
}

std::optional<Quantity_Color>
resolve_shape_color(const Handle(XCAFDoc_ColorTool)& color_tool,
                    const TDF_Label& label,
                    const TopoDS_Shape& shape) {
  if (color_tool.IsNull()) return std::nullopt;
  Quantity_Color c;
  if (!label.IsNull()) {
    if (color_tool->GetColor(label, XCAFDoc_ColorGen,  c)) return c;
    if (color_tool->GetColor(label, XCAFDoc_ColorSurf, c)) return c;
  }
  if (color_tool->GetColor(shape, XCAFDoc_ColorGen,  c)) return c;
  if (color_tool->GetColor(shape, XCAFDoc_ColorSurf, c)) return c;
  return std::nullopt;
}

std::string read_label_name(const TDF_Label& label) {
  if (label.IsNull()) return {};
  Handle(TDataStd_Name) name_attr;
  if (label.FindAttribute(TDataStd_Name::GetID(), name_attr)) {
    const TCollection_ExtendedString& es = name_attr->Get();
    std::string out;
    out.reserve(static_cast<std::size_t>(es.Length()));
    for (Standard_Integer i = 1; i <= es.Length(); ++i) {
      const Standard_ExtCharacter ch = es.Value(i);
      out.push_back(ch < 128 ? static_cast<char>(ch) : '?');
    }
    return out;
  }
  return {};
}

// Sample one edge's 3D curve at the given deflection tolerances and append
// the resulting polyline to `lod` as a GL_LINE_STRIP run terminated by the
// 0xFFFFFFFF primitive-restart sentinel. Points are in the shape's local
// frame because BRepAdaptor_Curve folds in edge.Location() internally.
// Returns true if a non-degenerate polyline was emitted.
//
// Why a strip + restart and NOT pair-indices for GL_LINES: the old layout
// pushed every interior polyline vertex into two consecutive segments, so
// the rasterizer produced two fragments at every joint pixel. With the
// alpha-blended ink in draw_edges (depth_mask off, no depth filtering),
// the joint pixel ended up blended twice — visibly darker than the rest of
// the line — and a row of evenly-spaced "dots" appeared along every curved
// edge. Strip + restart makes each joint a single fragment.
bool sample_edge_into_lod(scene::Mesh::EdgeLod& lod,
                          const TopoDS_Edge& edge,
                          double angular_deflection,
                          double linear_deflection) {
  BRepAdaptor_Curve curve(edge);
  std::vector<scene::vec3> pts;
  try {
    GCPnts_TangentialDeflection sampler(
      curve,
      static_cast<Standard_Real>(angular_deflection),
      static_cast<Standard_Real>(linear_deflection));
    const Standard_Integer n = sampler.NbPoints();
    if (n < 2) return false;
    pts.reserve(static_cast<std::size_t>(n));
    for (Standard_Integer i = 1; i <= n; ++i) {
      const gp_Pnt& p = sampler.Value(i);
      pts.emplace_back(static_cast<float>(p.X()),
                       static_cast<float>(p.Y()),
                       static_cast<float>(p.Z()));
    }
  } catch (...) {
    return false;
  }

  const auto base = static_cast<std::uint32_t>(lod.vertices.size());
  lod.vertices.insert(lod.vertices.end(), pts.begin(), pts.end());
  for (std::size_t i = 0; i < pts.size(); ++i) {
    lod.indices.push_back(base + static_cast<std::uint32_t>(i));
  }
  // Restart sentinel separates this polyline from the next so the renderer
  // can issue a single GL_LINE_STRIP draw across the whole LOD buffer
  // without the strips bleeding into each other. The renderer binds 0xFFFFFFFFu
  // via glPrimitiveRestartIndex; keep both sides in sync if it ever changes.
  lod.indices.push_back(0xFFFFFFFFu);
  return true;
}

// Bake a level-of-detail ladder of BRep edge polylines for the shape. Each
// tier is a full re-discretization at a finer chord-to-curve tolerance: tier
// 0 matches the surface mesh deflection (so edges and faces align at default
// zoom), tier 1 is 4x finer, tier 2 is 16x finer. The renderer picks one
// tier per frame from the camera's world-per-pixel scale and binds that
// tier's prebuilt GPU buffers — no runtime curve evaluation, no stutter.
//
// Straight lines collapse to 2 samples regardless of deflection, so the
// extra LODs are essentially free for boxy parts; curves get progressively
// smoother polylines without ever forcing the renderer to recompute them.
//
// Deduplication is by the underlying TShape pointer so an edge shared by
// two adjacent faces is only sampled once per tier. Degenerate edges (e.g.
// the pole-collapse "edge" of a sphere) carry no 3D curve and are skipped.
void extract_brep_edges(scene::Mesh& mesh,
                        const TopoDS_Shape& shape,
                        const ImportOptions& opts,
                        ConversionStats& stats) {
  using clock = std::chrono::steady_clock;
  // Tier deflection multipliers. The factor-of-4 spacing on linear
  // deflection gives a comfortable hysteresis band when the renderer picks
  // a tier from world-per-pixel: a flip requires zooming ~4x further, so
  // tier oscillation during slow zooms is impossible.
  struct Tier { double linear_mul; double angular_mul; };
  static constexpr Tier kTiers[] = {
    {1.0,    1.0   },  // coarse — surface-aligned
    {0.25,   0.5   },  // fine
    {0.0625, 0.25  },  // ultra
  };
  constexpr std::size_t kLodCount = sizeof(kTiers) / sizeof(kTiers[0]);

  mesh.edge_lods.resize(kLodCount);
  for (std::size_t t = 0; t < kLodCount; ++t) {
    mesh.edge_lods[t].linear_deflection =
      static_cast<float>(opts.linear_deflection * kTiers[t].linear_mul);
  }

  std::unordered_set<const void*> seen;
  for (TopExp_Explorer ex(shape, TopAbs_EDGE); ex.More(); ex.Next()) {
    const TopoDS_Edge& edge = TopoDS::Edge(ex.Current());
    if (BRep_Tool::Degenerated(edge)) continue;
    const void* key = edge.TShape().get();
    if (!seen.insert(key).second) continue;

    for (std::size_t t = 0; t < kLodCount; ++t) {
      const auto phase_start = clock::now();
      sample_edge_into_lod(
        mesh.edge_lods[t], edge,
        opts.angular_deflection * kTiers[t].angular_mul,
        opts.linear_deflection  * kTiers[t].linear_mul);
      if (opts.profile_timings) {
        add_timing(stats, "analytical edge LOD sampling",
                   clock::now() - phase_start);
      }
    }
  }

  // Drop trailing empty tiers (e.g. all edges failed to sample at the
  // finest tolerance) so the renderer's selection always lands on a tier
  // with geometry.
  while (!mesh.edge_lods.empty() && mesh.edge_lods.back().indices.empty()) {
    mesh.edge_lods.pop_back();
  }
}

// A shape is safe to backface-cull only when its triangles carry a consistent
// outward winding — that is, when it bounds a volume. STEP imports are
// typically TopoDS_Solids and qualify. IGES imports are usually a quilt of
// independent trimmed faces with no shell/solid topology, so each patch's
// orientation comes straight from its (arbitrary) surface parametrization;
// culling would silently drop every patch that happens to face away from the
// camera. Treat a shape as cull-safe if it contains a solid or a closed shell;
// the importer flags everything else double-sided.
bool is_cull_safe(const TopoDS_Shape& shape) {
  if (TopExp_Explorer(shape, TopAbs_SOLID).More()) return true;
  for (TopExp_Explorer ex(shape, TopAbs_SHELL); ex.More(); ex.Next()) {
    if (BRep_Tool::IsClosed(ex.Current())) return true;
  }
  return false;
}

} // namespace

std::shared_ptr<scene::Mesh>
shape_to_mesh(const TopoDS_Shape& shape,
              const ImportOptions& opts,
              const std::optional<Quantity_Color>& vertex_color_default,
              ConversionStats& stats) {
  using clock = std::chrono::steady_clock;
  if (shape.IsNull()) return nullptr;
  auto phase_start = clock::now();
  tessellate(shape, opts);
  if (opts.profile_timings) {
    add_timing(stats, "shape safety tessellation", clock::now() - phase_start);
  }

  auto mesh = std::make_shared<scene::Mesh>();
  std::uint32_t face_id = 0;
  phase_start = clock::now();
  std::unordered_set<const void*> seen_strip_edges;
  for (TopExp_Explorer ex(shape, TopAbs_FACE); ex.More(); ex.Next()) {
    const TopoDS_Face& face = TopoDS::Face(ex.Current());
    append_face(*mesh, face, vertex_color_default, opts, stats, face_id++,
                seen_strip_edges);
  }
  if (opts.profile_timings) {
    add_timing(stats, "face topology walk", clock::now() - phase_start);
  }
  if (mesh->indices.empty()) return nullptr;
  // Non-solid shapes (typically IGES surface quilts / open shells) have no
  // consistent outward winding; the renderer must draw them double-sided or
  // half of every part gets backface-culled away.
  phase_start = clock::now();
  mesh->double_sided = !is_cull_safe(shape);
  if (opts.profile_timings) {
    add_timing(stats, "cull-safety topology check", clock::now() - phase_start);
  }
  phase_start = clock::now();
  extract_brep_edges(*mesh, shape, opts, stats);
  if (opts.profile_timings) {
    add_timing(stats, "analytical edge LOD total", clock::now() - phase_start);
  }
  return mesh;
}

std::shared_ptr<scene::Scene>
document_to_scene(const opencascade::handle<TDocStd_Document>& doc,
                  const TopoDS_Shape& fallback_shape,
                  const ImportOptions& opts,
                  float unit_to_meters,
                  ConversionStats& stats,
                  IProgressSink& progress) {
  auto scn = std::make_shared<scene::Scene>();
  scn->unit_to_meters = unit_to_meters;

  // No-XDE fallback path — treat the whole shape as a single node.
  if (doc.IsNull()) {
    if (fallback_shape.IsNull()) return scn;
    auto phase_start = std::chrono::steady_clock::now();
    auto mesh = shape_to_mesh(fallback_shape, opts, std::nullopt, stats);
    if (opts.profile_timings) {
      add_timing(stats, "fallback shape conversion",
                 std::chrono::steady_clock::now() - phase_start);
    }
    if (!mesh) return scn;
    mesh->name = "root";
    const auto mesh_idx = scn->add_mesh(mesh);
    scene::Node root;
    root.name = "root";
    root.mesh_index = mesh_idx;
    root.source_label = "/root";
    scn->add_node(std::move(root));
    scn->add_material(scene::Material::brushed_metal());
    phase_start = std::chrono::steady_clock::now();
    scn->update_transforms();
    if (opts.profile_timings) {
      add_timing(stats, "scene transform update",
                 std::chrono::steady_clock::now() - phase_start);
    }
    return scn;
  }

  Handle(XCAFDoc_ShapeTool) shape_tool =
    XCAFDoc_DocumentTool::ShapeTool(doc->Main());
  Handle(XCAFDoc_ColorTool) color_tool =
    XCAFDoc_DocumentTool::ColorTool(doc->Main());

  // Allocate the default material slot so face submeshes can index it.
  scn->add_material(scene::Material::brushed_metal());

  TDF_LabelSequence labels;
  shape_tool->GetFreeShapes(labels);
  if (labels.IsEmpty()) {
    stats.diagnostics.push_back({DiagnosticSeverity::Warning,
                                 "XDE document had no free shapes."});
    return scn;
  }

  // Triangulate the whole document in a single pass. BRepMesh_IncrementalMesh
  // parallelizes across faces, so meshing every free shape at once keeps all
  // cores busy. Meshing per-part during the walk instead (as shape_to_mesh
  // still does below) serializes many small jobs, each with too few faces to
  // fill the thread pool — the slow path on large assemblies. Located instances
  // of a part share the same face TShapes, and a triangulation is stored in the
  // face's local frame, so one mesher covers every occurrence; afterwards the
  // per-part tessellate() calls find an adequate triangulation already attached
  // to each face and skip the heavy work (it stays as a correctness safety net
  // for any face this batch somehow missed).
  {
    const auto phase_start = std::chrono::steady_clock::now();
    TopoDS_Compound all;
    BRep_Builder builder;
    builder.MakeCompound(all);
    for (Standard_Integer i = 1; i <= labels.Length(); ++i) {
      TopoDS_Shape s;
      if (shape_tool->GetShape(labels.Value(i), s) && !s.IsNull()) {
        builder.Add(all, s);
      }
    }
    progress.update(0.45f, "Tessellating geometry...");
    tessellate(all, opts);
    if (opts.profile_timings) {
      add_timing(stats, "batch document tessellation",
                 std::chrono::steady_clock::now() - phase_start);
    }
  }
  if (progress.cancelled()) return scn;

  // Each "free shape" is treated as a separate root. We then expand the
  // assembly under it. Mesh sharing is keyed on the OCCT shape's TShape*
  // pointer so instanced parts upload once.
  std::unordered_map<const void*, std::uint32_t> mesh_cache;
  std::unordered_map<std::uint32_t, std::uint32_t> material_cache;

  auto material_for_color = [&](const std::optional<Quantity_Color>& c) -> std::uint32_t {
    if (!c) return 0;
    const std::uint32_t key = pack_color(*c);
    auto it = material_cache.find(key);
    if (it != material_cache.end()) return it->second;
    scene::Material m;
    m.name = "color_" + std::to_string(material_cache.size());
    m.base_color = scene::vec4(
      static_cast<float>(c->Red()),
      static_cast<float>(c->Green()),
      static_cast<float>(c->Blue()),
      1.0f);
    // Fully dielectric + matte. A faintly metallic / mid-roughness default
    // turns residual facet error into visible specular banding from the IBL
    // probe — see Material::plastic() for the same reasoning.
    m.metallic   = 0.0f;
    m.roughness  = 0.75f;
    const std::uint32_t idx = scn->add_material(std::move(m));
    material_cache.emplace(key, idx);
    return idx;
  };

  // Recursive descent. Each call creates one Scene::Node for `label` and
  // attaches/recurses based on the label's role in the XDE document:
  //
  //   - Assembly       : node has children, one per component
  //   - Reference      : node carries the instance location; geometry comes
  //                      from the prototype's shape (recursed into for nested
  //                      assemblies so nothing is lost)
  //   - SimpleShape    : node owns a Mesh directly
  //
  // The location returned by GetLocation(label) is the *instance* transform
  // for component labels and identity for prototype labels — exactly what we
  // need to place each occurrence in world space without manually
  // accumulating transforms during traversal.
  auto build_mesh_for = [&](const TopoDS_Shape& shape,
                            const TDF_Label& proto_label,
                            const std::optional<Quantity_Color>& fallback_color) -> std::uint32_t {
    const void* key = shape.TShape().get();
    auto cache_it = mesh_cache.find(key);
    if (cache_it != mesh_cache.end()) return cache_it->second;

    auto default_color = fallback_color;
    if (!default_color) {
      default_color = resolve_shape_color(color_tool, proto_label, shape);
    }
    const auto phase_start = std::chrono::steady_clock::now();
    auto mesh = shape_to_mesh(shape, opts, default_color, stats);
    if (opts.profile_timings) {
      add_timing(stats, "shape conversion total",
                 std::chrono::steady_clock::now() - phase_start);
    }
    if (!mesh) return scene::Scene::kInvalid;
    mesh->name = read_label_name(proto_label);
    if (mesh->name.empty()) mesh->name = "mesh";

    std::uint32_t sub_index = 0;
    for (TopExp_Explorer ex(shape, TopAbs_FACE);
         ex.More() && sub_index < mesh->submeshes.size();
         ex.Next(), ++sub_index) {
      auto fc = resolve_face_color(color_tool, ex.Current());
      mesh->submeshes[sub_index].material_index = fc
        ? material_for_color(fc)
        : material_for_color(default_color);
    }
    const auto idx = scn->add_mesh(mesh);
    mesh_cache.emplace(key, idx);
    return idx;
  };

  std::function<std::uint32_t(const TDF_Label&, std::uint32_t, const std::string&)> walk;
  walk = [&](const TDF_Label& label,
             std::uint32_t parent,
             const std::string& path) -> std::uint32_t {
    if (progress.cancelled()) return scene::Scene::kInvalid;
    if (!shape_tool->IsShape(label)) return scene::Scene::kInvalid;

    scene::Node node;
    node.parent = parent;
    node.name = read_label_name(label);
    if (node.name.empty()) node.name = "node";
    node.source_label = path + "/" + node.name;

    // Instance/local transform — pulled from the label, which carries the
    // per-instance placement for components and identity for prototypes.
    TopLoc_Location loc = shape_tool->GetLocation(label);
    if (!loc.IsIdentity()) {
      gp_Trsf trsf = loc.Transformation();
      scene::mat4 m(1.0f);
      for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 4; ++c)
          m[c][r] = static_cast<float>(trsf.Value(r + 1, c + 1));
      node.local = scene::Transform::from_matrix(m);
    }

    const std::uint32_t node_idx = scn->add_node(std::move(node));
    if (parent != scene::Scene::kInvalid) {
      scn->nodes[parent].children.push_back(node_idx);
    }

    auto recurse_children = [&](const TDF_Label& assembly_label) {
      TDF_LabelSequence components;
      shape_tool->GetComponents(assembly_label, components);
      for (Standard_Integer i = 1; i <= components.Length(); ++i) {
        walk(components.Value(i), node_idx,
             scn->nodes[node_idx].source_label);
      }
    };

    if (shape_tool->IsAssembly(label)) {
      recurse_children(label);
    } else if (shape_tool->IsReference(label)) {
      // Component: resolve the prototype and continue.
      TDF_Label proto;
      if (!shape_tool->GetReferredShape(label, proto)) return node_idx;
      if (shape_tool->IsAssembly(proto)) {
        // The prototype is itself an assembly — its components become our
        // node's children, sharing our instance transform.
        recurse_children(proto);
      } else if (shape_tool->IsSimpleShape(proto)) {
        TopoDS_Shape shape;
        shape_tool->GetShape(proto, shape);
        auto color = resolve_shape_color(color_tool, label, shape);
        if (!color) color = resolve_shape_color(color_tool, proto, shape);
        const auto mesh_idx = build_mesh_for(shape, proto, color);
        if (mesh_idx != scene::Scene::kInvalid) {
          scn->nodes[node_idx].mesh_index = mesh_idx;
        }
      }
    } else if (shape_tool->IsSimpleShape(label)) {
      TopoDS_Shape shape;
      shape_tool->GetShape(label, shape);
      auto color = resolve_shape_color(color_tool, label, shape);
      const auto mesh_idx = build_mesh_for(shape, label, color);
      if (mesh_idx != scene::Scene::kInvalid) {
        scn->nodes[node_idx].mesh_index = mesh_idx;
      }
    }
    return node_idx;
  };

  const auto walk_start = std::chrono::steady_clock::now();
  for (Standard_Integer i = 1; i <= labels.Length(); ++i) {
    if (progress.cancelled()) break;
    progress.update(static_cast<float>(i) / labels.Length(),
                    "Walking assembly...");
    walk(labels.Value(i), scene::Scene::kInvalid, "");
  }
  if (opts.profile_timings) {
    add_timing(stats, "assembly walk total",
               std::chrono::steady_clock::now() - walk_start);
  }

  const auto transform_start = std::chrono::steady_clock::now();
  scn->update_transforms();
  if (opts.profile_timings) {
    add_timing(stats, "scene transform update",
               std::chrono::steady_clock::now() - transform_start);
  }
  return scn;
}

} // namespace cadly::cad::occt

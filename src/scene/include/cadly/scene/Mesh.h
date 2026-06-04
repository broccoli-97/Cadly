#pragma once

#include "cadly/scene/Aabb.h"
#include "cadly/scene/Math.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cadly::scene {

// MVP vertex layout. Matches the OpenGL vertex attribute bindings in
// renderer_gl. Keep this packed so we can upload it straight to a VBO.
#pragma pack(push, 1)
struct Vertex {
  vec3   position;
  vec3   normal;
  std::uint32_t color_rgba8 {0xFFFFFFFFu}; // baked tint, optional
};
#pragma pack(pop)
static_assert(sizeof(Vertex) == 7 * 4,
              "Vertex layout drifted; renderer assumes 28 bytes.");

// A Submesh is a contiguous range of indices that share a material and an
// optional originating CAD face id. One Mesh can contain many submeshes for
// faceted models that retain per-face colors.
struct Submesh {
  std::uint32_t index_offset{0};
  std::uint32_t index_count {0};
  std::uint32_t material_index{0};
  std::uint32_t source_face_id{0}; // OCCT face id, for picking
  Aabb bounds = Aabb::empty();
};

// A Mesh owns the geometry buffers and the submesh table. Mesh data is
// renderer-agnostic; the renderer keeps its own GPU handle indexed by the
// Mesh's id slot in the Scene.
struct Mesh {
  std::string  name;
  std::vector<Vertex>        vertices;
  std::vector<std::uint32_t> indices;
  std::vector<Submesh>       submeshes;
  Aabb bounds = Aabb::empty();

  // True when these triangles are NOT guaranteed a consistent outward winding,
  // i.e. the source shape does not bound a volume: an IGES surface model (a
  // quilt of independent trimmed faces) or any open shell, as opposed to a
  // closed solid. The renderer must draw such a mesh double-sided — with
  // backface culling on, every patch whose arbitrary parametric orientation
  // happens to face away from the camera is culled and the "back" of the model
  // vanishes. Closed solids leave this false and keep the cheaper single-sided
  // cull. Set by the importer (see OcctShapeToMesh); orthogonal to a material
  // being intrinsically two-sided (Material::double_sided) — the renderer
  // culls only when neither flag asks for both faces.
  bool double_sided{false};

  // One level-of-detail in the BRep edge LOD ladder. Each LOD is a complete
  // GL_LINE_STRIP polyline set covering every edge of the source shape:
  // indices form a flat run where each per-edge strip is terminated by the
  // 0xFFFFFFFF primitive-restart sentinel, so a single drawcall covers the
  // whole tier. The renderer picks one tier per frame based on the camera's
  // current world-per-pixel scale (finer when zoomed in, coarser when zoomed
  // out). `linear_deflection` is the maximum chord-to-curve distance used to
  // bake this tier — the renderer compares it against the projected pixel
  // budget to select which tier to draw.
  struct EdgeLod {
    std::vector<vec3>          vertices;
    std::vector<std::uint32_t> indices;
    float linear_deflection {0.0f}; // world units (usually mm)
  };

  // BRep edge polylines extracted from the source CAD shape (NOT triangle
  // boundaries — these are the analytical edges of the brep, the curves
  // bounding faces). Stored as a LOD ladder sorted coarsest first; the
  // renderer selects a tier per frame based on the camera's pixel scale.
  // Empty for non-BRep meshes.
  //
  // Used for "edges without surfaces" views where there is no triangulated
  // face to anchor against; the LOD selector then chooses smoothness per
  // frame from the camera scale. NOT used for the "shaded with edges"
  // overlay — that path uses `edge_strip_indices` below, which is
  // guaranteed to sit exactly on the face triangulation.
  std::vector<EdgeLod> edge_lods;

  // Mesh-coupled BRep edge polylines: one GL_LINE_STRIP per edge,
  // terminated by the 0xFFFFFFFF primitive-restart sentinel, with indices
  // pointing straight into `vertices`. Sampled at exactly the face
  // triangulation's boundary nodes (via OCCT's Poly_PolygonOnTriangulation).
  // Because every edge vertex IS a face vertex, their depth values are
  // identical before glPolygonOffset is applied; the renderer's "shaded
  // with edges" overlay can rely on polygon offset alone to keep edges in
  // front of their faces — no Z fighting, no edges disappearing into the
  // surface where the analytical curve diverges from the chord polygon.
  //
  // Empty for non-BRep meshes (STL, OBJ, ...).
  std::vector<std::uint32_t> edge_strip_indices;

  std::size_t triangle_count() const { return indices.size() / 3; }
};

} // namespace cadly::scene

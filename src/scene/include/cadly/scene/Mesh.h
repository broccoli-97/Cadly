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

  // BRep edge polylines extracted from the source CAD shape (NOT triangle
  // boundaries — these are the analytical edges of the brep, the curves
  // bounding faces). Stored as a flat vec3 vertex array plus GL_LINES-style
  // index pairs, so a single glDrawElements(GL_LINES, ...) renders the whole
  // wireframe outline. Empty for non-BRep meshes.
  std::vector<vec3>          edge_vertices;
  std::vector<std::uint32_t> edge_indices;

  std::size_t triangle_count() const { return indices.size() / 3; }
  std::size_t edge_segment_count() const { return edge_indices.size() / 2; }
};

} // namespace cadly::scene

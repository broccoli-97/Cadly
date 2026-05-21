#pragma once

#include "cadly/scene/Math.h"

#include <cstdint>

namespace cadly::renderer {

// Opaque GPU handle. The renderer chooses how to populate the value; the rest
// of the code only checks against `invalid`.
struct ResourceHandle {
  std::uint64_t value{0};
  bool valid() const { return value != 0; }
  bool operator==(const ResourceHandle& o) const { return value == o.value; }
  bool operator!=(const ResourceHandle& o) const { return value != o.value; }
};

struct MeshHandle     : ResourceHandle {};
struct MaterialHandle : ResourceHandle {};

// Display options pushed in by the UI layer. The renderer interprets them; the
// scene itself doesn't need to know what's on.
struct DisplayMode {
  bool wireframe        {false};
  bool show_edges       {false};   // outline at sharp creases
  bool show_grid        {false};
  bool show_axes        {true};
  bool draw_background  {true};
  // Debug overlay: draw every triangle edge of the face triangulation on
  // top of the shaded surface. Independent of show_edges (BRep edges) and
  // wireframe (BRep-only line view); orthogonal to both.
  bool show_triangle_mesh {false};
  float edge_intensity  {0.65f};
  scene::vec3 background_top   {0.42f, 0.44f, 0.48f};
  scene::vec3 background_bottom{0.22f, 0.23f, 0.26f};

  // Rotation pivot indicator. The UI toggles this on while the user is
  // orbiting and feeds the world-space pivot through `rotation_pivot`. The
  // renderer draws a small screen-space-stable marker on top of the scene.
  bool        show_rotation_pivot{false};
  scene::vec3 rotation_pivot     {0.0f};
};

} // namespace cadly::renderer

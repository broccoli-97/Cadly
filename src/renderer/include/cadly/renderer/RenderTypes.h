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
  bool show_grid        {true};
  bool show_axes        {true};
  bool draw_background  {true};
  float edge_intensity  {0.65f};
  scene::vec3 background_top   {0.13f, 0.14f, 0.16f};
  scene::vec3 background_bottom{0.05f, 0.05f, 0.06f};
};

} // namespace cadly::renderer

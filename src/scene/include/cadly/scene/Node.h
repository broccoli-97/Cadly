#pragma once

#include "cadly/scene/Aabb.h"
#include "cadly/scene/Math.h"
#include "cadly/scene/Transform.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cadly::scene {

// Hierarchy node. Parent links use indices so the hierarchy can live in a
// flat vector and be traversed cache-coherently.
struct Node {
  static constexpr std::uint32_t kInvalid = static_cast<std::uint32_t>(-1);

  std::string name;
  std::uint32_t parent{kInvalid};
  std::vector<std::uint32_t> children;

  Transform local;
  mat4 world_matrix{1.0f};       // refreshed during Scene::update_transforms
  Aabb world_bounds = Aabb::empty();

  std::optional<std::uint32_t> mesh_index; // index into Scene::meshes

  bool visible{true};
  // Viewer-state flags written by the UI and read by the renderer, mirroring
  // `visible`. `selected` marks the sidebar's current pick — the renderer
  // tints those surfaces with DisplayMode::selection_color so the user can
  // match a tree row to its geometry. `ghosted` marks everything OUTSIDE the
  // isolate focus (sidebar double-click): those nodes draw as a translucent
  // veil at DisplayMode::ghost_opacity instead of full surfaces. Importers
  // leave both at their defaults.
  bool selected{false};
  bool ghosted{false};

  // CAD provenance — kept here, not in Mesh, because the same Mesh can be
  // instanced under many nodes that all came from different assembly parts.
  std::string source_label;      // e.g. "/assembly/bracket/M6_screw_1"
  std::string layer;
  std::optional<std::uint32_t> material_override;
};

} // namespace cadly::scene

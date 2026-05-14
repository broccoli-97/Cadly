#pragma once

#include "cadly/scene/Aabb.h"
#include "cadly/scene/Camera.h"
#include "cadly/scene/LightEnvironment.h"
#include "cadly/scene/Material.h"
#include "cadly/scene/Mesh.h"
#include "cadly/scene/Node.h"
#include "cadly/scene/SelectionId.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace cadly::scene {

// A Scene is the renderer-/Qt-/OCCT-independent canonical model. Importers
// produce one, the renderer consumes it.
struct Scene {
  std::vector<std::shared_ptr<Mesh>> meshes;
  std::vector<Material>              materials;
  std::vector<Node>                  nodes;

  Camera           camera;
  LightEnvironment environment;
  Aabb             world_bounds = Aabb::empty();

  // CAD provenance metadata.
  std::filesystem::path source_file;
  std::string           source_unit{"mm"};  // STEP often gives "MM"
  float                 unit_to_meters{0.001f};

  std::uint32_t add_node(Node&& n);
  std::uint32_t add_material(Material&& m);
  std::uint32_t add_mesh(std::shared_ptr<Mesh> m);

  // Refresh world matrices and bounds top-down. Call after import or after any
  // local-transform edit.
  void update_transforms();

  // Linear find by source label; returns kInvalid if absent. Used by UI to
  // resolve picks from the tree.
  std::uint32_t find_node_by_label(const std::string& label) const;

  static constexpr std::uint32_t kInvalid = static_cast<std::uint32_t>(-1);
};

} // namespace cadly::scene

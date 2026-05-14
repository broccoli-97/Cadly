#include "cadly/scene/Scene.h"

#include <queue>
#include <utility>

namespace cadly::scene {

std::uint32_t Scene::add_node(Node&& n) {
  const auto idx = static_cast<std::uint32_t>(nodes.size());
  nodes.emplace_back(std::move(n));
  return idx;
}

std::uint32_t Scene::add_material(Material&& m) {
  const auto idx = static_cast<std::uint32_t>(materials.size());
  materials.emplace_back(std::move(m));
  return idx;
}

std::uint32_t Scene::add_mesh(std::shared_ptr<Mesh> m) {
  const auto idx = static_cast<std::uint32_t>(meshes.size());
  meshes.push_back(std::move(m));
  return idx;
}

void Scene::update_transforms() {
  world_bounds = Aabb::empty();
  if (nodes.empty()) return;

  // Roots are nodes without a parent. We allow many roots so importers can put
  // every shape under a synthetic root or keep them flat.
  std::vector<std::uint32_t> stack;
  for (std::uint32_t i = 0; i < nodes.size(); ++i) {
    if (nodes[i].parent == Node::kInvalid) stack.push_back(i);
  }

  // Iterative depth-first traversal — keeps stack usage bounded for deep
  // assemblies.
  std::vector<std::uint32_t> order;
  order.reserve(nodes.size());
  while (!stack.empty()) {
    const auto idx = stack.back();
    stack.pop_back();
    order.push_back(idx);
    for (auto c : nodes[idx].children) stack.push_back(c);
  }

  for (auto idx : order) {
    Node& n = nodes[idx];
    const mat4 local = n.local.to_matrix();
    if (n.parent == Node::kInvalid) {
      n.world_matrix = local;
    } else {
      n.world_matrix = nodes[n.parent].world_matrix * local;
    }
    n.world_bounds = Aabb::empty();
    if (n.mesh_index && *n.mesh_index < meshes.size() && meshes[*n.mesh_index]) {
      const Mesh& mesh = *meshes[*n.mesh_index];
      n.world_bounds = mesh.bounds.transformed(n.world_matrix);
    }
    world_bounds.expand(n.world_bounds);
  }

  // After local bounds are set, propagate child bounds up to parents so the
  // tree exposes hierarchical AABBs (useful for picking and isolate).
  for (auto it = order.rbegin(); it != order.rend(); ++it) {
    Node& n = nodes[*it];
    for (auto c : n.children) {
      n.world_bounds.expand(nodes[c].world_bounds);
    }
  }
}

std::uint32_t Scene::find_node_by_label(const std::string& label) const {
  for (std::uint32_t i = 0; i < nodes.size(); ++i) {
    if (nodes[i].source_label == label) return i;
  }
  return kInvalid;
}

} // namespace cadly::scene

#pragma once

#include "cadly/scene/Scene.h"

#include <cstdint>
#include <string>

namespace cadly::cad {

// Diagnostic comparison of imports made by the same build. This is neither
// a file-cache key nor a cross-version geometric equivalence test.
struct SceneFingerprint {
  std::uint64_t geometry{14695981039346656037ULL};
  std::uint64_t scene{14695981039346656037ULL};
};

inline SceneFingerprint scene_fingerprint(const scene::Scene& scn) {
  auto bytes = [](std::uint64_t& hash, const void* data, std::size_t size) {
    const auto* p = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
      hash ^= p[i];
      hash *= 1099511628211ULL;
    }
  };
  SceneFingerprint result;
  auto number = [&](std::uint64_t value) { bytes(result.scene, &value, sizeof(value)); };
  auto string = [&](const std::string& value) {
    number(value.size());
    bytes(result.scene, value.data(), value.size());
  };
  auto vector = [&](const scene::vec3& value) { bytes(result.scene, &value, sizeof(value)); };
  number(scn.meshes.size());
  for (const auto& mesh : scn.meshes) {
    number(mesh ? 1 : 0);
    if (!mesh) continue;
    string(mesh->name);
    number(mesh->vertices.size());
    number(mesh->indices.size());
    number(mesh->double_sided);
    for (const auto& vertex : mesh->vertices) {
      bytes(result.geometry, &vertex.position, sizeof(vertex.position));
      bytes(result.geometry, &vertex.normal, sizeof(vertex.normal));
      number(vertex.color_rgba8);
    }
    bytes(result.geometry, mesh->indices.data(), mesh->indices.size() * sizeof(std::uint32_t));
    number(mesh->submeshes.size());
    for (const auto& sub : mesh->submeshes) {
      number(sub.index_offset); number(sub.index_count);
      number(sub.source_face_id); number(sub.material_index);
    }
    number(mesh->edge_strip_indices.size());
    bytes(result.scene, mesh->edge_strip_indices.data(), mesh->edge_strip_indices.size() * sizeof(std::uint32_t));
    number(mesh->edge_lods.size());
    for (const auto& lod : mesh->edge_lods) {
      bytes(result.scene, &lod.linear_deflection, sizeof(lod.linear_deflection));
      number(lod.vertices.size());
      for (const auto& point : lod.vertices) vector(point);
      number(lod.indices.size());
      bytes(result.scene, lod.indices.data(), lod.indices.size() * sizeof(std::uint32_t));
    }
  }
  number(result.geometry);
  number(scn.nodes.size());
  for (const auto& node : scn.nodes) {
    string(node.name); string(node.source_label); string(node.layer);
    number(node.parent);
    number(node.children.size());
    for (const auto child : node.children) number(child);
    number(node.mesh_index.value_or(scene::Scene::kInvalid));
    number(node.material_override.value_or(scene::Scene::kInvalid));
    for (int c = 0; c < 4; ++c)
      for (int r = 0; r < 4; ++r)
        bytes(result.scene, &node.world_matrix[c][r], sizeof(float));
  }
  number(scn.materials.size());
  for (const auto& material : scn.materials) {
    string(material.name);
    bytes(result.scene, &material.base_color, sizeof(material.base_color));
    bytes(result.scene, &material.metallic, sizeof(float));
    bytes(result.scene, &material.roughness, sizeof(float));
    number(material.double_sided);
  }
  vector(scn.world_bounds.min); vector(scn.world_bounds.max);
  bytes(result.scene, &scn.unit_to_meters, sizeof(float));
  return result;
}

} // namespace cadly::cad

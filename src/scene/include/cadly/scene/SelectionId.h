#pragma once

#include <cstdint>

namespace cadly::scene {

// Stable handle for picking. Two-tier so a hit can resolve to a part as well as
// a face within that part without a follow-up lookup.
struct SelectionId {
  std::uint32_t node{0};       // index into Scene::nodes
  std::uint32_t submesh{0};    // index into Mesh::submeshes
  std::uint32_t face{0};       // OCCT face id, when known

  bool operator==(const SelectionId& other) const {
    return node == other.node && submesh == other.submesh && face == other.face;
  }
  bool operator!=(const SelectionId& other) const { return !(*this == other); }
  bool valid() const { return node != 0 || submesh != 0 || face != 0; }
};

} // namespace cadly::scene

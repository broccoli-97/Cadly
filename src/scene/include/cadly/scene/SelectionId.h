#pragma once

#include <cstdint>
#include <limits>

namespace cadly::scene {

// Stable handle for picking. Two-tier so a hit can resolve to a part as well as
// a face within that part without a follow-up lookup.
//
// All three slots default to `kInvalid` (== uint32_t(-1)) so a default-
// constructed instance compares unequal to any real hit and `valid()` returns
// false. Using a dedicated sentinel matters because `node == 0` is a perfectly
// good first-node index — the previous "any field non-zero" rule mis-flagged
// it as invalid.
struct SelectionId {
  static constexpr std::uint32_t kInvalid =
    std::numeric_limits<std::uint32_t>::max();

  std::uint32_t node   {kInvalid};       // index into Scene::nodes
  std::uint32_t submesh{kInvalid};       // index into Mesh::submeshes
  std::uint32_t face   {kInvalid};       // OCCT face id, when known

  bool operator==(const SelectionId& other) const {
    return node == other.node && submesh == other.submesh && face == other.face;
  }
  bool operator!=(const SelectionId& other) const { return !(*this == other); }

  // A pick is meaningful as soon as we know which node it landed on; submesh
  // and face are refinements that may or may not be available depending on
  // backend support.
  bool valid() const { return node != kInvalid; }
};

} // namespace cadly::scene

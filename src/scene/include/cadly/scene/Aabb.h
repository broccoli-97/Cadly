#pragma once

#include "cadly/scene/Math.h"

#include <limits>

namespace cadly::scene {

// Axis-aligned bounding box in some implicit coordinate space. The "empty"
// state is represented by min > max on every axis.
struct Aabb {
  vec3 min{ std::numeric_limits<float>::infinity()};
  vec3 max{-std::numeric_limits<float>::infinity()};

  static Aabb empty() { return Aabb{}; }

  bool valid() const {
    return min.x <= max.x && min.y <= max.y && min.z <= max.z;
  }

  vec3 center() const { return 0.5f * (min + max); }
  vec3 extent() const { return max - min; }
  float radius() const {
    if (!valid()) return 0.0f;
    return 0.5f * glm::length(extent());
  }

  void expand(const vec3& p);
  void expand(const Aabb& other);

  // Transform the box and re-fit. Cheap conservative box.
  Aabb transformed(const mat4& m) const;
};

} // namespace cadly::scene

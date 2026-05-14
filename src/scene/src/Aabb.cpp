#include "cadly/scene/Aabb.h"

#include <algorithm>

namespace cadly::scene {

void Aabb::expand(const vec3& p) {
  min = glm::min(min, p);
  max = glm::max(max, p);
}

void Aabb::expand(const Aabb& other) {
  if (!other.valid()) return;
  min = glm::min(min, other.min);
  max = glm::max(max, other.max);
}

Aabb Aabb::transformed(const mat4& m) const {
  if (!valid()) return Aabb::empty();
  Aabb out = Aabb::empty();
  const vec3 corners[8] = {
    {min.x, min.y, min.z}, {max.x, min.y, min.z},
    {min.x, max.y, min.z}, {max.x, max.y, min.z},
    {min.x, min.y, max.z}, {max.x, min.y, max.z},
    {min.x, max.y, max.z}, {max.x, max.y, max.z},
  };
  for (const auto& c : corners) {
    const vec4 t = m * vec4(c, 1.0f);
    out.expand(vec3(t));
  }
  return out;
}

} // namespace cadly::scene

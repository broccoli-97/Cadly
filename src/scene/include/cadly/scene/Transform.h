#pragma once

#include "cadly/scene/Math.h"

namespace cadly::scene {

// Compact, decomposed local transform. Stored in TRS form so animation and
// hierarchy updates can interpolate without losing scale/orientation info.
struct Transform {
  vec3 translation{0.0f};
  quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; // identity
  vec3 scale{1.0f};

  mat4 to_matrix() const {
    mat4 m = glm::translate(mat4(1.0f), translation);
    m *= glm::toMat4(rotation);
    m = glm::scale(m, scale);
    return m;
  }

  static Transform from_matrix(const mat4& m) {
    Transform t;
    t.translation = vec3(m[3]);
    const vec3 sx(m[0]);
    const vec3 sy(m[1]);
    const vec3 sz(m[2]);
    t.scale = {glm::length(sx), glm::length(sy), glm::length(sz)};
    mat3 r;
    r[0] = sx / t.scale.x;
    r[1] = sy / t.scale.y;
    r[2] = sz / t.scale.z;
    t.rotation = glm::quat_cast(r);
    return t;
  }
};

} // namespace cadly::scene

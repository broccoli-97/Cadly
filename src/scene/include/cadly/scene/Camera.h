#pragma once

#include "cadly/scene/Math.h"

namespace cadly::scene {

// Orbit-style camera tailored to CAD inspection: a target point, a yaw/pitch
// rotation, and a distance. View math is derived on demand so the caller can
// freely tweak yaw/pitch/distance without ordering concerns.
struct Camera {
  vec3 target{0.0f};
  float yaw   {glm::radians(30.0f)}; // around Y
  float pitch {glm::radians(-22.0f)}; // around camera-right
  float distance{5.0f};

  float fov_y  {glm::radians(45.0f)};
  float near_z {0.05f};
  float far_z  {1500.0f};
  float aspect {1.0f};

  vec3 position() const;
  vec3 forward()  const;
  vec3 right()    const;
  vec3 up()       const;

  mat4 view()       const;
  mat4 projection() const;
  mat4 view_proj()  const { return projection() * view(); }

  // Frame the given world-space bounds with a small margin. `fit_factor` of
  // 1.0 places the box exactly inside the view frustum; larger numbers leave
  // padding around it.
  void frame_bounds(const vec3& min, const vec3& max, float fit_factor = 1.4f);
};

} // namespace cadly::scene

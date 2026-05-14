#include "cadly/scene/Camera.h"

#include <algorithm>
#include <cmath>

namespace cadly::scene {

namespace {
// Clamp pitch to avoid flipping at the poles while still allowing the user to
// look nearly straight up/down — a common CAD camera need.
constexpr float kPitchEpsilon = 0.001f;
}

vec3 Camera::position() const {
  const float cp = std::cos(pitch);
  const vec3 dir = {
    cp * std::sin(yaw),
    std::sin(pitch),
    cp * std::cos(yaw),
  };
  return target - dir * distance;
}

vec3 Camera::forward() const {
  return glm::normalize(target - position());
}

vec3 Camera::right() const {
  return glm::normalize(glm::cross(forward(), vec3(0.0f, 1.0f, 0.0f)));
}

vec3 Camera::up() const {
  return glm::normalize(glm::cross(right(), forward()));
}

mat4 Camera::view() const {
  return glm::lookAt(position(), target, vec3(0.0f, 1.0f, 0.0f));
}

mat4 Camera::projection() const {
  return glm::perspective(fov_y, std::max(aspect, 0.0001f), near_z, far_z);
}

void Camera::frame_bounds(const vec3& min, const vec3& max, float fit_factor) {
  const vec3 center = 0.5f * (min + max);
  const vec3 extent = max - min;
  const float radius = 0.5f * glm::length(extent);
  target = center;

  // Distance such that the bounding sphere fits the smaller of the two FOVs.
  const float fov_x = 2.0f * std::atan(std::tan(0.5f * fov_y) * aspect);
  const float fov = std::min(fov_y, fov_x);
  distance = (radius / std::sin(0.5f * fov)) * fit_factor;

  // Reasonable near/far around the bounding sphere.
  near_z = std::max(distance - radius * 4.0f, radius * 0.005f);
  far_z  = distance + radius * 8.0f;
  if (far_z <= near_z) far_z = near_z + 1.0f;

  pitch = std::clamp(pitch,
                     -glm::half_pi<float>() + kPitchEpsilon,
                      glm::half_pi<float>() - kPitchEpsilon);
}

} // namespace cadly::scene

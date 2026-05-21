#include "cadly/scene/Camera.h"

#include <algorithm>
#include <cmath>

namespace cadly::scene {

namespace {
constexpr vec3 kWorldUp{0.0f, 1.0f, 0.0f};
}

vec3 Camera::position() const {
  return target - forward() * distance;
}

vec3 Camera::forward() const {
  // OpenGL convention: camera looks along its local -Z.
  return orientation * vec3(0.0f, 0.0f, -1.0f);
}

vec3 Camera::right() const {
  return orientation * vec3(1.0f, 0.0f, 0.0f);
}

vec3 Camera::up() const {
  return orientation * vec3(0.0f, 1.0f, 0.0f);
}

mat4 Camera::view() const {
  // view = inverse(translate(position) * rotate(orientation))
  //      = rotate(inverse(orientation)) * translate(-position)
  return glm::mat4_cast(glm::inverse(orientation)) *
         glm::translate(mat4(1.0f), -position());
}

mat4 Camera::projection() const {
  const float a = std::max(aspect, 0.0001f);
  if (projection_mode == Projection::Perspective) {
    return glm::perspective(fov_y, a, near_z, far_z);
  }
  // Match the apparent height at the target plane so toggling preserves
  // on-screen scale, and so distance-based zoom keeps behaving the same.
  const float half_h = distance * std::tan(0.5f * fov_y);
  const float half_w = half_h * a;
  return glm::ortho(-half_w, half_w, -half_h, half_h, near_z, far_z);
}

void Camera::rotate_around(const vec3& pivot, const quat& delta) {
  // Rotate the target offset from the pivot, then update the orientation by
  // the same delta. Because `position()` is derived as
  //     position = target - (orientation * -Z) * distance
  // applying `delta` to both `target - pivot` and `orientation` yields
  //     new_position - pivot = delta * (position - pivot)
  // i.e. the eye orbits the pivot too, and `distance` is preserved.
  target      = pivot + delta * (target - pivot);
  orientation = glm::normalize(delta * orientation);
}

void Camera::orbit(float yaw_delta, float pitch_delta, const vec3& pivot) {
  // Clamp pitch to keep the camera right-side up. Composition of quaternions
  // is mathematically clean, but a yaw-around-world-up + pitch-around-camera-
  // right ("turntable") camera does NOT stay user-coherent once the camera
  // pitches past ±90° elevation: the camera flips upside down, world-up
  // no longer matches the user's perceived up, and yaw around world-up then
  // looks reversed on screen. Quaternions don't rescue this — the issue is
  // user-frame vs world-frame, not the rotation representation. So clamp the
  // resulting pitch to a hair under ±π/2 and feed the clamped delta into the
  // rotation. `forward().y` is the sine of the current elevation in the
  // no-roll steady state (right() lies in the world-XZ plane, so neither
  // yaw nor pitch ever leaks roll), so `asin(forward.y)` is the current
  // pitch angle.
  const float fwd_y       = glm::clamp(forward().y, -1.0f, 1.0f);
  const float cur_pitch   = std::asin(fwd_y);
  const float pitch_limit = glm::half_pi<float>() - glm::radians(1.0f);
  const float new_pitch   = glm::clamp(cur_pitch + pitch_delta,
                                       -pitch_limit, pitch_limit);
  const float pitch_eff   = new_pitch - cur_pitch;

  // Yaw around the world-up axis. This never injects roll — world-Y is a fixed
  // direction independent of the camera's current orientation.
  const quat q_yaw = glm::angleAxis(yaw_delta, kWorldUp);

  // Pitch around the camera-right axis AFTER yaw, so the response matches the
  // user's expectation across the full sphere. `right()` is in the world XZ
  // plane in the no-roll steady state; `q_yaw * right()` is the right axis
  // the user sees once yaw has been applied.
  const vec3 right_after_yaw = glm::normalize(q_yaw * right());
  const quat q_pitch = glm::angleAxis(pitch_eff, right_after_yaw);

  rotate_around(pivot, q_pitch * q_yaw);
}

void Camera::set_orientation_yaw_pitch(float yaw, float pitch) {
  // The +π on the yaw rotates the camera-local -Z (which is "look direction")
  // to face +Z at yaw=0, matching the historical Euler convention so that
  // existing presets and frame_bounds() output land in the same view.
  const quat q_yaw   = glm::angleAxis(yaw + glm::pi<float>(), kWorldUp);
  const quat q_pitch = glm::angleAxis(pitch, vec3(1.0f, 0.0f, 0.0f));
  orientation = glm::normalize(q_yaw * q_pitch);
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
}

} // namespace cadly::scene

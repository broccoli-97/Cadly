#pragma once

#include "cadly/scene/Math.h"

namespace cadly::scene {

// Orbit-style camera tailored to CAD inspection: a target point, a quaternion
// orientation, and a distance. View math is derived on demand so callers can
// freely tweak the state without ordering concerns.
//
// The orientation is stored as a quaternion (rather than yaw+pitch Euler
// angles) so rotations compose without gimbal headaches and so the camera can
// be orbited around an arbitrary world-space pivot — see `rotate_around()`.
// CAD-style "no roll" behaviour is preserved by always feeding orbit input
// through `orbit()`, which decomposes the delta into yaw-around-world-up and
// pitch-around-camera-right.
enum class Projection {
  Orthographic,
  Perspective,
};

struct Camera {
  vec3 target{0.0f};

  // Camera-to-world rotation. The identity points the camera down -Z (standard
  // OpenGL convention); the default value here matches the previous
  // (yaw=30°, pitch=-22°) Euler defaults so the initial view is unchanged.
  quat orientation{
    glm::angleAxis(glm::pi<float>() + glm::radians(30.0f), vec3(0.0f, 1.0f, 0.0f)) *
    glm::angleAxis(glm::radians(-22.0f), vec3(1.0f, 0.0f, 0.0f))
  };

  float distance{5.0f};

  float fov_y  {glm::radians(45.0f)};
  float near_z {0.05f};
  float far_z  {1500.0f};
  float aspect {1.0f};

  // CAD inspection traditionally defaults to orthographic so that parallel
  // features stay parallel on screen and measurements are not foreshortened.
  // Ortho extents are derived from `distance` and `fov_y` (see projection()),
  // which keeps zoom-via-distance and frame_bounds() working in both modes.
  Projection projection_mode{Projection::Orthographic};

  vec3 position() const;
  vec3 forward()  const;   // unit direction from eye toward target
  vec3 right()    const;
  vec3 up()       const;

  mat4 view()       const;
  mat4 projection() const;
  mat4 view_proj()  const { return projection() * view(); }

  // Rotate the camera around `pivot` by `delta` (a world-space rotation).
  // Both `target` and the implicit eye position rotate around the pivot;
  // `distance` is preserved by construction. When `pivot == target` this is
  // pure orbit-around-target, but the pivot may also live anywhere else,
  // which is what the controller exploits for "rotate around picked point"
  // and similar features.
  void rotate_around(const vec3& pivot, const quat& delta);

  // Convenience for orbit-style mouse input: composes a delta that is
  //   - yaw_delta around the world up axis (no roll added)
  //   - pitch_delta around the camera-right axis after yaw
  // and applies it around `pivot`. Use this from input controllers.
  void orbit(float yaw_delta, float pitch_delta, const vec3& pivot);

  // Reset the orientation from a yaw/pitch pair. Convenient for view-cube
  // buttons (front, top, isometric, …) and for any caller that thinks in
  // Euler angles.
  void set_orientation_yaw_pitch(float yaw, float pitch);

  // Frame the given world-space bounds with a small margin. `fit_factor` of
  // 1.0 places the box exactly inside the view frustum; larger numbers leave
  // padding around it.
  void frame_bounds(const vec3& min, const vec3& max, float fit_factor = 1.4f);
};

} // namespace cadly::scene

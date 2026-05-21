#include "cadly/ui/CameraController.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace cadly::ui {

namespace {
constexpr float kOrbitSpeed = 0.006f;   // rad/pixel
constexpr float kPanSpeed   = 0.0025f;
constexpr float kZoomSpeed  = 0.0025f;
constexpr float kWheelSpeed = 0.0015f;
}

RotationPivot TargetPivotResolver::resolve(const scene::Camera& camera,
                                           QPoint) const {
  return RotationPivot{camera.target};
}

CameraController::CameraController(QObject* parent)
  : QObject(parent),
    pivot_resolver_(std::make_unique<TargetPivotResolver>()) {}

void CameraController::set_viewport(int w, int h) {
  viewport_w_ = std::max(1, w);
  viewport_h_ = std::max(1, h);
  camera_.aspect = static_cast<float>(viewport_w_) /
                   static_cast<float>(viewport_h_);
  emit changed();
}

void CameraController::frame_bounds(const scene::vec3& min,
                                    const scene::vec3& max) {
  // Cache the scene's bounding sphere so subsequent zoom/orbit/pan can
  // (a) keep the camera outside the model and (b) keep near/far adapted
  // so the model never gets clipped, no matter how close the user zooms.
  scene_center_ = 0.5f * (min + max);
  scene_radius_ = std::max(0.5f * glm::length(max - min), 1e-4f);
  camera_.aspect = static_cast<float>(viewport_w_) /
                   static_cast<float>(viewport_h_);
  camera_.frame_bounds(min, max);
  update_clip_planes();
  emit changed();
}

float CameraController::clamp_distance(float requested) const {
  // Hard floor: never let distance drop below a tiny epsilon — the camera
  // math degenerates at 0.
  constexpr float kAbsoluteMin = 1e-4f;
  float d = std::max(requested, kAbsoluteMin);
  if (scene_radius_ <= 0.0f) return d;

  // Camera position parameterised by distance:
  //     c(d) = target - forward * d
  // We want |c(d) - scene_center| >= R for R = scene_radius * safety, i.e.
  //     d^2 - 2(f·v)d + (v·v - R^2) >= 0      where v = target - scene_center
  // The quadratic opens upward, so the disallowed band sits between its
  // two roots. The orbit camera typically starts with d > upper_root
  // (camera outside on the "near target" side); zoom-in then has to stop
  // at upper_root or it would punch into the bounding sphere.
  const scene::vec3 v = camera_.target - scene_center_;
  const scene::vec3 f = camera_.forward();
  const float R       = scene_radius_ * 1.02f;        // 2% safety margin
  const float v_dot_v = glm::dot(v, v);
  const float f_dot_v = glm::dot(f, v);
  const float disc    = f_dot_v * f_dot_v - (v_dot_v - R * R);
  if (disc <= 0.0f) return d;                         // always outside; no clamp

  const float d_min_outside = f_dot_v + std::sqrt(disc);
  return std::max(d, std::max(d_min_outside, kAbsoluteMin));
}

void CameraController::update_clip_planes() {
  // Re-derive near/far from the actual camera-to-scene-center distance.
  // The static near/far set once by Camera::frame_bounds becomes wrong as
  // soon as the user zooms: the model's front face ends up closer to the
  // camera than the original near plane and gets clipped, which looks
  // exactly like "the camera is passing through the model."
  if (scene_radius_ <= 0.0f) return;
  const float d_to_center  = glm::length(camera_.position() - scene_center_);
  const float r            = scene_radius_;
  const float near_to_face = std::max(d_to_center - r, 0.0f);
  // Half the distance to the nearest model point gives the model plenty of
  // headroom, while the absolute floor (a small fraction of `distance`)
  // keeps depth precision sane when the user has zoomed very close.
  camera_.near_z = std::max(near_to_face * 0.5f, camera_.distance * 0.001f);
  camera_.far_z  = (d_to_center + r) * 2.0f + 1.0f;
  if (camera_.far_z <= camera_.near_z) camera_.far_z = camera_.near_z + 1.0f;
}

void CameraController::set_rotation_pivot_resolver(
    std::unique_ptr<RotationPivotResolver> r) {
  pivot_resolver_ = r ? std::move(r)
                      : std::make_unique<TargetPivotResolver>();
}

void CameraController::begin_drag(DragMode mode, QPoint at) {
  drag_mode_ = mode;
  last_pos_  = at;

  if (mode == DragMode::Orbit) {
    // Lock in the pivot for the entire drag. Resolving per-mouse-move would
    // make the model squirm as the cursor passed over different geometry.
    rotation_pivot_ = pivot_resolver_->resolve(camera_, at).world_position;
    emit rotation_pivot_visibility_changed(rotation_pivot_, true);
  }
}

void CameraController::update_drag(QPoint to) {
  if (drag_mode_ == DragMode::None) return;
  const QPoint delta = to - last_pos_;
  last_pos_ = to;

  switch (drag_mode_) {
    case DragMode::Orbit: {
      // Negate so the camera moves opposite to the mouse, matching the
      // "grab the world and drag it" feel of the previous Euler controller.
      const float yaw_delta   = -delta.x() * kOrbitSpeed;
      const float pitch_delta = -delta.y() * kOrbitSpeed;
      camera_.orbit(yaw_delta, pitch_delta, rotation_pivot_);
      break;
    }
    case DragMode::Pan: {
      // Translate target by camera-space basis scaled to viewport units.
      const float scale = camera_.distance * kPanSpeed;
      const scene::vec3 right = camera_.right();
      const scene::vec3 up    = camera_.up();
      camera_.target -= right * (float)delta.x() * scale;
      camera_.target += up    * (float)delta.y() * scale;
      break;
    }
    case DragMode::Dolly: {
      const float factor = 1.0f + delta.y() * kZoomSpeed;
      camera_.distance = clamp_distance(camera_.distance * factor);
      break;
    }
    default: break;
  }
  // Every drag step shifts the camera relative to the scene, so the near/
  // far planes derived from that distance need to follow.
  update_clip_planes();
  emit changed();
}

void CameraController::end_drag() {
  const bool was_orbiting = drag_mode_ == DragMode::Orbit;
  drag_mode_ = DragMode::None;
  if (was_orbiting) {
    emit rotation_pivot_visibility_changed(rotation_pivot_, false);
  }
}

void CameraController::wheel(int angle_delta) {
  const float factor = std::exp(-static_cast<float>(angle_delta) * kWheelSpeed);
  camera_.distance = clamp_distance(camera_.distance * factor);
  update_clip_planes();
  emit changed();
}

} // namespace cadly::ui

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

// Safety margin on the scene's bounding sphere for the perspective
// stay-outside rule: the eye is kept at ≥ radius × this, so the model's
// extreme points (which touch the sphere) can never reach the near plane.
constexpr float kOutsideMargin = 1.05f;
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

void CameraController::restore_camera(const scene::Camera& camera,
                                      const scene::vec3& min,
                                      const scene::vec3& max) {
  scene_center_ = 0.5f * (min + max);
  scene_radius_ = std::max(0.5f * glm::length(max - min), 1e-4f);
  camera_ = camera;
  camera_.aspect = static_cast<float>(viewport_w_) /
                   static_cast<float>(viewport_h_);
  update_clip_planes();
  emit changed();
}

float CameraController::min_outside_distance() const {
  // Smallest `Camera::distance` for which the eye sits outside the scene's
  // bounding sphere (inflated by kOutsideMargin). The eye moves along the
  // ray  position(d) = target − forward·d  as the user zooms, and `target`
  // is generally NOT the sphere centre (cursor-anchored zoom and panning
  // move it), so this is a ray/sphere intersection, not a plain radius
  // comparison. With o = target − centre and f = forward, solving
  // |o − f·d| = r for d gives  d = o·f ± sqrt((o·f)² − |o|² + r²);  the eye
  // is inside the sphere exactly between the two roots, so the larger root
  // is the zoom-in limit. A negative discriminant means the eye ray misses
  // the sphere entirely — the user is zooming past the model, which can
  // never put the eye inside it, so no limit applies.
  if (scene_radius_ <= 0.0f) return 0.0f;
  const scene::vec3 o = camera_.target - scene_center_;
  const scene::vec3 f = camera_.forward();
  const float r    = scene_radius_ * kOutsideMargin;
  const float of   = glm::dot(o, f);
  const float disc = of * of - glm::dot(o, o) + r * r;
  if (disc <= 0.0f) return 0.0f;
  return of + std::sqrt(disc);
}

float CameraController::clamp_distance(float requested) const {
  constexpr float kAbsoluteMin = 1e-4f;
  if (camera_.projection_mode == scene::Projection::Orthographic) {
    // Under parallel projection the eye position has no optical meaning —
    // zoom is pure magnification (the ortho extents derive from `distance`),
    // and update_clip_planes() fits the depth slab around the whole model no
    // matter where the eye sits. So ortho zoom is unlimited: the user can
    // magnify a single feature indefinitely and the model is never cut open.
    return std::max(requested, kAbsoluteMin);
  }
  // Perspective is different: the near plane must sit at z > 0 in front of
  // the eye, so once the eye enters the model the near plane inevitably
  // slices it ("inside the model" cutaway). Keep the eye outside the scene's
  // bounding sphere instead — zooming in runs up to the model and stops at
  // its surface rather than passing through. The sphere is a conservative
  // stand-in for the real surface until depth-based picking exists. If the
  // eye is somehow already inside (camera restored from an older session),
  // don't yank it outward — just refuse to go deeper.
  const float d_min = std::min(min_outside_distance(), camera_.distance);
  return std::max(std::max(requested, d_min), kAbsoluteMin);
}

void CameraController::set_projection(scene::Projection mode) {
  camera_.projection_mode = mode;
  if (mode == scene::Projection::Perspective) {
    // Deep ortho zoom may have parked the eye inside the model — invisible
    // and harmless under parallel projection, but perspective gives the eye
    // position optical meaning again. Hop it back outside the bounding
    // sphere; the apparent zoom level changes, which beats slicing the model
    // open the moment the user presses P.
    camera_.distance = std::max(camera_.distance, min_outside_distance());
  }
  update_clip_planes();
  emit changed();
}

void CameraController::update_clip_planes() {
  // Re-derive near/far from the actual camera and scene geometry on every
  // interaction. A static near/far set once at frame time becomes wrong as
  // soon as the user zooms.
  if (scene_radius_ <= 0.0f) return;
  const float r = scene_radius_;
  if (camera_.projection_mode == scene::Projection::Orthographic) {
    // Fit the clip slab around the entire model, wherever the eye happens to
    // be. `near` may legitimately go negative (model behind the eye plane):
    // GL orthographic projection allows it, and it is exactly what makes
    // deep ortho zoom a magnification instead of a cutaway — the model can
    // never poke out of the slab, so it is never sliced by the near plane.
    const float d_center = glm::dot(scene_center_ - camera_.position(),
                                    camera_.forward());
    camera_.near_z = d_center - r * kOutsideMargin;
    camera_.far_z  = d_center + r * kOutsideMargin;
    return;
  }
  // Perspective: near must stay positive. clamp_distance() keeps the eye
  // outside the bounding sphere, so `near_to_face` (eye to the sphere's
  // front) stays positive too; half of it gives the model headroom while
  // the absolute floor (a small fraction of `distance`) keeps depth
  // precision sane at the closest allowed approach.
  const float d_to_center  = glm::length(camera_.position() - scene_center_);
  const float near_to_face = std::max(d_to_center - r, 0.0f);
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
      if (orbit_style_ == OrbitStyle::Turntable) {
        camera_.orbit_turntable(yaw_delta, pitch_delta, rotation_pivot_);
      } else {
        camera_.orbit(yaw_delta, pitch_delta, rotation_pivot_);
      }
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

void CameraController::wheel(QPoint cursor_pos, int angle_delta) {
  const float factor       = std::exp(-static_cast<float>(angle_delta) * kWheelSpeed);
  const float old_distance = camera_.distance;
  const float new_distance = clamp_distance(old_distance * factor);
  const float ratio        = (old_distance > 0.0f) ? (new_distance / old_distance) : 1.0f;

  // Cursor in NDC (Qt y-down → GL y-up).
  const float ndc_x = (2.0f * static_cast<float>(cursor_pos.x()) /
                       static_cast<float>(viewport_w_)) - 1.0f;
  const float ndc_y = 1.0f - (2.0f * static_cast<float>(cursor_pos.y()) /
                              static_cast<float>(viewport_h_));

  // Focal-plane extents — same formula Camera::projection() uses for the ortho
  // path, which keeps the anchor consistent across both projection modes. The
  // anchor sits on the plane through `target` perpendicular to `forward`; for
  // perspective this plane is exactly where view-space depth equals `distance`,
  // so when we scale (target − cursor_world) by `ratio` the cursor's
  // homogeneous divide cancels (depth and offset both scale by ratio) and the
  // world point stays under the cursor. For ortho the proof is even simpler:
  // half_w/half_h scale linearly with `distance`, so the cursor's NDC is
  // preserved by construction.
  const float half_h = old_distance * std::tan(0.5f * camera_.fov_y);
  const float half_w = half_h * camera_.aspect;
  const scene::vec3 cursor_world =
      camera_.target + camera_.right() * (ndc_x * half_w)
                     + camera_.up()    * (ndc_y * half_h);

  // Shrink/expand the (target − cursor_world) offset by `ratio` so the world
  // point under the cursor projects to the same pixel after the distance
  // change. When ratio == 1 (distance clamped) target is unchanged.
  camera_.target   = cursor_world + (camera_.target - cursor_world) * ratio;
  camera_.distance = new_distance;
  update_clip_planes();
  emit changed();
}

void CameraController::set_view(float yaw_deg, float pitch_deg) {
  // Standard-view presets reorient only; preserving target+distance keeps the
  // user's current focal point and zoom, which matches what CAD tools do when
  // you tap a view-cube face. Clip planes still need a refresh because the
  // camera position is derived from orientation, so it shifts relative to the
  // scene center even though `distance` is unchanged.
  camera_.set_orientation_yaw_pitch(glm::radians(yaw_deg),
                                    glm::radians(pitch_deg));
  update_clip_planes();
  emit changed();
}

} // namespace cadly::ui

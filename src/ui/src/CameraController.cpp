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
  camera_.aspect = static_cast<float>(viewport_w_) /
                   static_cast<float>(viewport_h_);
  camera_.frame_bounds(min, max);
  emit changed();
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
      camera_.distance = std::max(0.001f, camera_.distance * factor);
      break;
    }
    default: break;
  }
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
  camera_.distance = std::max(0.001f, camera_.distance * factor);
  emit changed();
}

} // namespace cadly::ui

#include "cadly/ui/CameraController.h"

#include <algorithm>
#include <cmath>

namespace cadly::ui {

namespace {
constexpr float kOrbitSpeed = 0.006f;   // rad/pixel
constexpr float kPanSpeed   = 0.0025f;
constexpr float kZoomSpeed  = 0.0025f;
constexpr float kWheelSpeed = 0.0015f;
}

CameraController::CameraController(QObject* parent) : QObject(parent) {}

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

void CameraController::begin_drag(DragMode mode, QPoint at) {
  drag_mode_ = mode;
  last_pos_  = at;
}

void CameraController::update_drag(QPoint to) {
  if (drag_mode_ == DragMode::None) return;
  const QPoint delta = to - last_pos_;
  last_pos_ = to;

  switch (drag_mode_) {
    case DragMode::Orbit: {
      camera_.yaw   -= delta.x() * kOrbitSpeed;
      camera_.pitch -= delta.y() * kOrbitSpeed;
      const float lim = 1.5707f - 0.01f;
      camera_.pitch = std::clamp(camera_.pitch, -lim, lim);
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
  drag_mode_ = DragMode::None;
}

void CameraController::wheel(int angle_delta) {
  const float factor = std::exp(-static_cast<float>(angle_delta) * kWheelSpeed);
  camera_.distance = std::max(0.001f, camera_.distance * factor);
  emit changed();
}

} // namespace cadly::ui

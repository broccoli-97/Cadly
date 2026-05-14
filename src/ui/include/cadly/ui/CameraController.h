#pragma once

#include "cadly/scene/Camera.h"

#include <QObject>
#include <QPoint>

namespace cadly::ui {

// Orbit/pan/zoom controller. Owns no widget; the host viewport feeds mouse
// events in and consults `camera()` after each step.
class CameraController : public QObject {
  Q_OBJECT
public:
  explicit CameraController(QObject* parent = nullptr);

  scene::Camera& camera() { return camera_; }
  const scene::Camera& camera() const { return camera_; }

  void set_viewport(int w, int h);
  void frame_bounds(const scene::vec3& min, const scene::vec3& max);

  enum class DragMode { None, Orbit, Pan, Dolly };

  void begin_drag(DragMode mode, QPoint at);
  void update_drag(QPoint to);
  void end_drag();
  void wheel(int angle_delta);

signals:
  void changed();

private:
  scene::Camera camera_;
  DragMode      drag_mode_{DragMode::None};
  QPoint        last_pos_{};
  int           viewport_w_{1};
  int           viewport_h_{1};
};

} // namespace cadly::ui

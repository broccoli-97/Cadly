#pragma once

#include "cadly/scene/Camera.h"

#include <QObject>
#include <QPoint>

#include <memory>

namespace cadly::ui {

// Where a rotate-drag should pivot. Resolvers (below) build one of these at
// the moment the user presses the rotate button; the controller then uses it
// for every mouse-move event in the same drag.
struct RotationPivot {
  scene::vec3 world_position{0.0f};
};

// Strategy interface for choosing the rotation pivot. The default
// implementation (`TargetPivotResolver`) returns the camera's current target,
// which reproduces the historical "orbit around target" behaviour. Future
// resolvers can supply different pivots without touching the controller:
//
//   - PickedPointPivotResolver — ray-cast against the scene at `screen_pos`
//     and pivot around the hit point, so the model rotates "under the
//     cursor".
//   - SelectionPivotResolver   — pivot around the bounding-box centre of the
//     current selection.
//   - WorldOriginPivotResolver — pivot around (0, 0, 0) for layout work.
//
// All a new resolver needs to do is implement `resolve()` and hand an
// instance to `CameraController::set_rotation_pivot_resolver()`.
class RotationPivotResolver {
public:
  virtual ~RotationPivotResolver() = default;
  virtual RotationPivot resolve(const scene::Camera& camera,
                                QPoint                screen_pos) const = 0;
};

// Default pivot strategy: rotate around the camera's current target. Kept as
// a concrete class (not an inline lambda) so callers can re-create it after
// swapping in a custom resolver and back.
class TargetPivotResolver final : public RotationPivotResolver {
public:
  RotationPivot resolve(const scene::Camera& camera, QPoint) const override;
};

// Orbit/pan/zoom controller. Owns no widget; the host viewport feeds mouse
// events in and consults `camera()` after each step. Rotation is performed
// with quaternions around a pivot resolved at the start of each drag — see
// `set_rotation_pivot_resolver()` for how to swap in custom pivot strategies.
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

  // `cursor_pos` is widget-local pixels (Qt convention: origin top-left).
  // Zoom anchors on the world point currently under the cursor on the focal
  // plane through `target`, so that point stays under the cursor after the
  // distance change. Falls back to target-centred zoom near screen centre
  // (where `cursor_world ≈ target` makes the two formulations identical).
  void wheel(QPoint cursor_pos, int angle_delta);

  // Re-orient the camera to a fixed yaw/pitch (in degrees) without moving the
  // target or changing distance. Drives the View > Standard Views actions
  // (Front, Top, Right, Iso, …). Yaw/pitch follow the same convention as
  // scene::Camera::set_orientation_yaw_pitch: yaw=0,pitch=0 places the camera
  // on -Z looking toward +Z (the "front" face), pitch=-90 looks straight down.
  void set_view(float yaw_deg, float pitch_deg);

  // Replace the rotation-pivot strategy. Passing `nullptr` restores the
  // default `TargetPivotResolver`. Safe to call mid-session; the next
  // `begin_drag(Orbit, ...)` will use the new resolver.
  void set_rotation_pivot_resolver(std::unique_ptr<RotationPivotResolver> r);

  // True while an orbit drag is in progress.
  bool is_rotating() const { return drag_mode_ == DragMode::Orbit; }

  // The world-space pivot captured at the start of the current orbit drag.
  // Only meaningful while `is_rotating()` is true; for non-rotation states
  // the value is the most recent pivot (kept around so the renderer can fade
  // the indicator out if desired).
  const scene::vec3& rotation_pivot() const { return rotation_pivot_; }

signals:
  void changed();

  // Emitted when the rotation pivot becomes visible (mouse-down on orbit) or
  // hidden (mouse-up). Carries the world-space pivot so listeners can drive
  // an overlay or marker without having to know about the controller's
  // internal state.
  void rotation_pivot_visibility_changed(scene::vec3 pivot, bool visible);

private:
  // Clamp a requested zoom distance so the resulting camera position stays
  // outside the scene's bounding sphere with a small safety margin. With no
  // scene loaded (`scene_radius_ == 0`) only an absolute minimum is
  // enforced.
  float clamp_distance(float requested) const;

  // Recompute Camera::near_z / far_z from the current camera position and
  // the cached scene bounds, so the model is never clipped at any zoom
  // level. Cheap; safe to call on every input event.
  void  update_clip_planes();

  scene::Camera camera_;
  DragMode      drag_mode_{DragMode::None};
  QPoint        last_pos_{};
  int           viewport_w_{1};
  int           viewport_h_{1};

  std::unique_ptr<RotationPivotResolver> pivot_resolver_;
  scene::vec3                            rotation_pivot_{0.0f};

  // Cached scene bounding sphere — populated by frame_bounds(). The
  // controller uses this to (a) clamp zoom so the camera can't pass through
  // the model, and (b) keep near/far adapted to the current view so close-up
  // zoom doesn't clip the model's front face.
  scene::vec3 scene_center_{0.0f};
  float       scene_radius_{0.0f};
};

} // namespace cadly::ui

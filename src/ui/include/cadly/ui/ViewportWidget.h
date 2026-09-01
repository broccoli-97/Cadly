#pragma once

#include "cadly/renderer/IRenderer.h"
#include "cadly/renderer/RenderTypes.h"
#include "cadly/scene/Scene.h"
#include "cadly/ui/NavigationScheme.h"

#include <QElapsedTimer>
#include <QOpenGLWidget>

#include <memory>

namespace cadly::ui {

class CameraController;

// QOpenGLWidget that hosts the renderer. Provides the GL context, forwards
// input events to the camera controller, and pushes the scene + display mode
// to the renderer once per paint.
class ViewportWidget : public QOpenGLWidget {
  Q_OBJECT
public:
  explicit ViewportWidget(QWidget* parent = nullptr);
  ~ViewportWidget() override;

  // `fit` is true for a newly imported document and false when returning to a
  // tab whose camera should be restored.
  void set_scene(std::shared_ptr<scene::Scene> scene, bool fit = true);
  void set_display_mode(const renderer::DisplayMode& mode);
  renderer::DisplayMode& display_mode() { return display_mode_; }

  CameraController* camera_controller() { return camera_; }

  // Mouse-binding scheme for orbit/pan/zoom drags (persisted by the shell).
  void set_navigation_scheme(NavigationScheme scheme) { nav_scheme_ = scheme; }
  NavigationScheme navigation_scheme() const { return nav_scheme_; }

  // Re-frame the scene to fit current world bounds (toolbar "Fit").
  void fit_view();

signals:
  // Smoothed CPU cost of one paintGL pass, in milliseconds, emitted at most a
  // few times per second. Deliberately labelled "frame time" and not "fps" by
  // consumers: rendering is event-driven (paints only on input/dirty), so a
  // frames-per-second figure would be a lie most of the time.
  void frame_timed(float ms);

  // Left press that landed on empty space. Picking is not implemented yet,
  // so today EVERY left press that isn't consumed by the section gizmo counts
  // as empty space and the shell uses it to clear the selection highlight; a
  // future pick handler must consume presses that hit geometry before emitting
  // this.
  void background_clicked();

  // The section plane changed because the user dragged its manipulator. The
  // widget has already applied it to its own display mode (so the frame it is
  // about to paint is correct), and emits this so the shell can persist it per
  // document tab and refresh the banner readout. Mirrors how the rotation pivot
  // is bridged from the camera controller.
  //
  // `part` says which handle did it and `degrees` is how far the current
  // rotation drag has turned (0 for a translate drag), so the shell can show a
  // live angle while the user is tilting and drop the axis-preset check marks
  // once the plane is no longer on one.
  void section_plane_dragged(renderer::SectionPlane plane,
                             renderer::SectionGizmoPart part, float degrees);

  // A section drag ended. The shell uses it to take the transient angle readout
  // back off the banner.
  void section_drag_finished();

protected:
  void initializeGL() override;
  void resizeGL(int w, int h) override;
  void paintGL() override;

  void mousePressEvent(QMouseEvent*  e) override;
  void mouseMoveEvent (QMouseEvent*  e) override;
  void mouseReleaseEvent(QMouseEvent* e) override;
  void wheelEvent     (QWheelEvent*  e) override;

private:
  // Screen-space hit-test on the whole section manipulator: the translate
  // shaft, then the three rotate rings. Returns the piece under `pos` (logical
  // widget pixels), or None. Screen-space rather than GPU picking because
  // IRenderer::pick() is still a stub — and because this is how gizmos are
  // normally hit-tested anyway.
  renderer::SectionGizmoPart hit_section_gizmo(QPoint pos) const;
  // Map a cursor position to a section offset along the plane normal, clamped to
  // the model's travel. Returns false when the geometry is degenerate (plane
  // normal nearly parallel to the view), in which case the caller must hold the
  // current offset rather than let it jump.
  bool section_offset_at(QPoint pos, float& out_offset) const;
  // World units per pixel in the space the RENDERER draws in (device pixels).
  // Every hit-test has to work here, not in the logical pixels Qt reports, or
  // the grab region misses the drawn handle by the device pixel ratio.
  float section_world_per_device_pixel() const;
  // Angle of `pos` around `part`'s ring, in the ring's own frame. Takes the
  // plane explicitly because a drag must measure against the plane as it was at
  // mouse-down: tilting an off-centre plane slides its anchor, and re-deriving
  // the ring from the live plane would let the ring centre chase the cursor
  // that is turning it. Returns false when the ring is too edge-on to map a
  // cursor to an angle at all — see the screen-tangent fallback in
  // update_section_rotation().
  bool section_ring_angle_at_cursor(const renderer::SectionPlane& plane,
                                    renderer::SectionGizmoPart part, QPoint pos,
                                    float& out_angle) const;
  // Advance the in-progress rotation drag to `pos`, snapping to a drafting
  // increment while `snap` is set.
  void update_section_rotation(QPoint pos, bool snap);

  CameraController* camera_{nullptr};
  std::unique_ptr<renderer::IRenderer> renderer_;
  std::shared_ptr<scene::Scene> scene_;
  // True between a `set_scene` call and the next paint that re-attaches the
  // new scene to the renderer. Avoids the per-frame attach call that the
  // previous version did against the IRenderer contract.
  bool                  scene_dirty_{false};
  renderer::DisplayMode display_mode_{};
  // True while a camera drag is in flight (begin_drag .. end_drag). Lets
  // mouseMoveEvent promote a modifier-less left drag to orbit/pan when Alt
  // arrives mid-gesture (see the comment there).
  bool                  camera_drag_active_{false};
  NavigationScheme      nav_scheme_{NavigationScheme::Cadly};

  // Frame-time readout state: exponential moving average + emit throttle.
  float         frame_ms_avg_{0.0f};
  QElapsedTimer frame_emit_throttle_;

  // Section manipulator drag. None means no drag is in progress.
  renderer::SectionGizmoPart section_drag_part_{
    renderer::SectionGizmoPart::None};
  // Translate: the offset difference between where the cursor pointed at
  // mouse-down and the plane's offset then, so the handle stays put under the
  // cursor instead of snapping its centre to it.
  float section_drag_grab_{0.0f};
  // Rotate: the plane as it was when the drag began, plus the angle grabbed on
  // the ring. Every move re-derives the plane from these rather than
  // accumulating onto the live one, so a long drag cannot drift and releasing
  // Shift returns exactly to the unsnapped angle.
  renderer::SectionPlane section_drag_start_plane_{};
  float                  section_drag_start_angle_{0.0f};
  // Rotate: how far this drag has turned the plane so far, in radians. Normally
  // re-derived absolutely from the cursor, but advanced incrementally from
  // screen-space motion while the ring is too edge-on for that — holding the
  // total here (rather than one variable per mapping) is what lets a drag cross
  // between the two without the plane jumping.
  float  section_drag_angle_{0.0f};
  QPoint section_drag_last_pos_{};
};

} // namespace cadly::ui

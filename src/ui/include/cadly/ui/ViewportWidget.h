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
  // so today EVERY left press counts as empty space and the shell uses it to
  // clear the selection highlight; a future pick handler must consume
  // presses that hit geometry before emitting this.
  void background_clicked();

protected:
  void initializeGL() override;
  void resizeGL(int w, int h) override;
  void paintGL() override;

  void mousePressEvent(QMouseEvent*  e) override;
  void mouseMoveEvent (QMouseEvent*  e) override;
  void mouseReleaseEvent(QMouseEvent* e) override;
  void wheelEvent     (QWheelEvent*  e) override;

private:
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
};

} // namespace cadly::ui

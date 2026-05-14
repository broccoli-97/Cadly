#pragma once

#include "cadly/renderer/IRenderer.h"
#include "cadly/renderer/RenderTypes.h"
#include "cadly/scene/Scene.h"

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

  void set_scene(std::shared_ptr<scene::Scene> scene);
  void set_display_mode(const renderer::DisplayMode& mode);
  renderer::DisplayMode& display_mode() { return display_mode_; }

  CameraController* camera_controller() { return camera_; }

  // Re-frame the scene to fit current world bounds (toolbar "Fit").
  void fit_view();

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
  renderer::DisplayMode display_mode_{};
};

} // namespace cadly::ui

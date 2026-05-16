#include "cadly/ui/ViewportWidget.h"

#include "cadly/ui/CameraController.h"
#include "cadly/renderer_gl/GLRenderer.h"

#include <QMouseEvent>
#include <QSurfaceFormat>
#include <QWheelEvent>

#include <algorithm>

namespace cadly::ui {

ViewportWidget::ViewportWidget(QWidget* parent)
  : QOpenGLWidget(parent),
    camera_(new CameraController(this)) {

  QSurfaceFormat fmt;
  fmt.setVersion(4, 1);
  fmt.setProfile(QSurfaceFormat::CoreProfile);
  fmt.setDepthBufferSize(24);
  fmt.setStencilBufferSize(8);
  fmt.setSamples(4); // light MSAA — good for CAD silhouettes
  fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
  setFormat(fmt);

  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);

  connect(camera_, &CameraController::changed, this,
          QOverload<>::of(&ViewportWidget::update));

  // Mirror the controller's pivot state into the renderer's display mode so a
  // marker appears while the user is rotating. Keeping the bridging here (and
  // not inside the controller) leaves the controller free of renderer types.
  connect(camera_, &CameraController::rotation_pivot_visibility_changed,
          this, [this](scene::vec3 pivot, bool visible) {
            display_mode_.show_rotation_pivot = visible;
            display_mode_.rotation_pivot      = pivot;
            update();
          });
}

ViewportWidget::~ViewportWidget() {
  // Tear down GL resources while the context is still current.
  if (renderer_) {
    makeCurrent();
    renderer_->shutdown();
    doneCurrent();
  }
}

void ViewportWidget::initializeGL() {
  renderer_ = renderer_gl::make_gl_renderer();
  renderer_->initialize();
}

void ViewportWidget::resizeGL(int w, int h) {
  // QOpenGLWidget passes logical pixels, but glViewport needs device pixels.
  // Without this, the scene draws into the lower-left corner on HiDPI displays.
  const qreal dpr = devicePixelRatioF();
  const int w_px = std::max(1, static_cast<int>(w * dpr));
  const int h_px = std::max(1, static_cast<int>(h * dpr));
  if (renderer_) renderer_->resize(w_px, h_px);
  camera_->set_viewport(w, h);
}

void ViewportWidget::paintGL() {
  if (!renderer_) return;
  if (scene_) {
    scene_->camera = camera_->camera();
    renderer_->attach_scene(scene_);
  }
  renderer_->render(display_mode_);
}

void ViewportWidget::set_scene(std::shared_ptr<scene::Scene> scene) {
  scene_ = std::move(scene);
  if (scene_ && scene_->world_bounds.valid()) {
    camera_->frame_bounds(scene_->world_bounds.min, scene_->world_bounds.max);
  }
  update();
}

void ViewportWidget::set_display_mode(const renderer::DisplayMode& mode) {
  display_mode_ = mode;
  update();
}

void ViewportWidget::fit_view() {
  if (!scene_ || !scene_->world_bounds.valid()) return;
  camera_->frame_bounds(scene_->world_bounds.min, scene_->world_bounds.max);
}

void ViewportWidget::mousePressEvent(QMouseEvent* e) {
  using DM = CameraController::DragMode;
  DM mode = DM::None;
  if (e->button() == Qt::LeftButton)        mode = DM::Orbit;
  else if (e->button() == Qt::MiddleButton) mode = DM::Pan;
  else if (e->button() == Qt::RightButton)  mode = DM::Dolly;
  if (mode != DM::None) {
    camera_->begin_drag(mode, e->pos());
    setCursor(Qt::ClosedHandCursor);
    e->accept();
  } else {
    QOpenGLWidget::mousePressEvent(e);
  }
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* e) {
  camera_->update_drag(e->pos());
  QOpenGLWidget::mouseMoveEvent(e);
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent* e) {
  camera_->end_drag();
  setCursor(Qt::ArrowCursor);
  QOpenGLWidget::mouseReleaseEvent(e);
}

void ViewportWidget::wheelEvent(QWheelEvent* e) {
  camera_->wheel(e->angleDelta().y());
  e->accept();
}

} // namespace cadly::ui

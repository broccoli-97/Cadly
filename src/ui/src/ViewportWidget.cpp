#include "cadly/ui/ViewportWidget.h"

#include "cadly/ui/CameraController.h"
#include "cadly/renderer_gl/GLRenderer.h"

#include <QMouseEvent>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <QWheelEvent>

#include <algorithm>

namespace cadly::ui {

ViewportWidget::ViewportWidget(QWidget* parent)
  : QOpenGLWidget(parent),
    camera_(new CameraController(this)) {

  // MSAA is handled inside the GL renderer via an offscreen multisample
  // framebuffer (DisplayMode::msaa_samples controls quality). The default
  // framebuffer stays single-sample on purpose so the resolve blit at end
  // of frame is well-defined regardless of which sample count the user
  // picks at runtime — see GLRenderer's ensure_msaa_target.
  QSurfaceFormat fmt;
  fmt.setVersion(4, 1);
  fmt.setProfile(QSurfaceFormat::CoreProfile);
  fmt.setDepthBufferSize(24);
  fmt.setStencilBufferSize(8);
  fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
  setFormat(fmt);

  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  // Right-button is used for orbit. Suppress the system context menu so a
  // right-click-drag doesn't pop a menu mid-rotation.
  setContextMenuPolicy(Qt::PreventContextMenu);

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
  // Bridge Qt's GL entry-point resolver to the renderer's transport-neutral
  // loader signature. QOpenGLContext::getProcAddress returns a
  // QFunctionPointer (void(*)()); reinterpret_cast to void* is the same
  // implementation-defined cast every native loader on every platform does,
  // so it works wherever Qt's OpenGL works.
  auto* ctx = QOpenGLContext::currentContext();
  renderer_gl::GLLoadProc loader =
    [ctx](const char* name) -> void* {
      if (!ctx) return nullptr;
      return reinterpret_cast<void*>(ctx->getProcAddress(name));
    };
  renderer_ = renderer_gl::make_gl_renderer(std::move(loader));
  renderer_->initialize();

  // A `set_scene` may have run before the GL context existed; flush it now
  // so the first paint draws against the right mesh cache.
  if (scene_dirty_) {
    renderer_->attach_scene(scene_);
    scene_dirty_ = false;
  }
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
  if (scene_dirty_) {
    renderer_->attach_scene(scene_);
    scene_dirty_ = false;
  }
  if (scene_) {
    scene_->camera = camera_->camera();
  }
  renderer_->render(display_mode_);
}

void ViewportWidget::set_scene(std::shared_ptr<scene::Scene> scene) {
  scene_ = std::move(scene);
  scene_dirty_ = true;
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
  // Right-button orbits, middle-button pans. Left-button is reserved for
  // picking/highlight (not yet implemented) — passed through to the base
  // class so a future pick handler can consume it.
  if      (e->button() == Qt::RightButton)  mode = DM::Orbit;
  else if (e->button() == Qt::MiddleButton) mode = DM::Pan;
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

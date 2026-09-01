#include "cadly/ui/ViewportWidget.h"

#include "cadly/ui/CameraController.h"
#include "cadly/renderer/SectionPlane.h"
#include "cadly/renderer_gl/GLRenderer.h"

#include <QMouseEvent>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace cadly::ui {

namespace {
// Grab radius for the section translate handle, in logical pixels. Generous on
// purpose: the shaft is thin on screen, and a manipulator you have to aim at is
// a manipulator users stop reaching for.
constexpr float kHandleGrabPx = 14.0f;

// Rings get a tighter radius than the shaft. Three of them pass close to the
// gizmo centre, and a generous band there would make which one you grabbed a
// coin flip.
constexpr float kRingGrabPx = 10.0f;

// Screen-space samples per ring for the hit-test. 64 puts the chord error of a
// 78px ring well under a tenth of a pixel — far below the grab radius.
constexpr int kRingSamples = 64;

// Shift snaps a rotation drag to this increment. 15 degrees is the drafting
// convention and divides the useful angles (30/45/60/90) exactly.
constexpr float kRotateSnapDeg = 15.0f;

constexpr float kPi    = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

// Finite-difference step for the ring's screen-space tangent, in radians.
constexpr float kTangentEps = 0.01f;

Qt::CursorShape cursor_for_part(renderer::SectionGizmoPart part, bool dragging) {
  using Part = renderer::SectionGizmoPart;
  if (part == Part::Translate) return Qt::SizeVerCursor;
  if (part == Part::None)      return Qt::ArrowCursor;
  return dragging ? Qt::ClosedHandCursor : Qt::OpenHandCursor;
}
}  // namespace

ViewportWidget::ViewportWidget(QWidget* parent)
  : QOpenGLWidget(parent),
    camera_(new CameraController(this)) {

  // MSAA is handled inside the GL renderer via an offscreen multisample
  // framebuffer (DisplayMode::msaa_samples controls quality). The default
  // framebuffer stays single-sample on purpose so the resolve blit at end
  // of frame is well-defined regardless of which sample count the user
  // picks at runtime — see GLRenderer's ensure_msaa_target.
  //
  // setAlphaBufferSize(0) marks the surface as fully opaque to the OS
  // compositor; mirrors main.cpp because QOpenGLWidget uses the widget-
  // local format and not setDefaultFormat when both are present.
  QSurfaceFormat fmt;
  fmt.setVersion(4, 1);
  fmt.setProfile(QSurfaceFormat::CoreProfile);
  fmt.setDepthBufferSize(24);
  fmt.setStencilBufferSize(8);
  fmt.setAlphaBufferSize(0);
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

  // CPU-side cost of building and submitting the frame. Not a GPU time (that
  // would need timer queries), but it tracks the work the app actually does
  // per paint and is what the status-bar readout wants.
  QElapsedTimer t;
  t.start();
  renderer_->render(display_mode_);
  if (renderer_->needs_redraw()) update();
  const float ms = static_cast<float>(t.nsecsElapsed()) / 1.0e6f;
  frame_ms_avg_ = frame_ms_avg_ <= 0.0f ? ms
                                        : frame_ms_avg_ * 0.8f + ms * 0.2f;
  if (!frame_emit_throttle_.isValid() ||
      frame_emit_throttle_.elapsed() > 250) {
    frame_emit_throttle_.restart();
    emit frame_timed(frame_ms_avg_);
  }
}

void ViewportWidget::set_scene(std::shared_ptr<scene::Scene> scene, bool fit) {
  // Camera changes normally reach Scene in paintGL. A tab switch may happen
  // before that queued paint, so persist the controller state explicitly.
  if (scene_) scene_->camera = camera_->camera();
  scene_ = std::move(scene);
  scene_dirty_ = true;
  if (scene_ && scene_->world_bounds.valid()) {
    if (fit) {
      camera_->restore_camera(scene_->camera, scene_->world_bounds.min,
                              scene_->world_bounds.max);
      camera_->frame_bounds(scene_->world_bounds.min, scene_->world_bounds.max);
    } else {
      camera_->restore_camera(scene_->camera, scene_->world_bounds.min,
                              scene_->world_bounds.max);
    }
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

float ViewportWidget::section_world_per_device_pixel() const {
  // The renderer sizes the manipulator in DEVICE pixels (its glViewport is
  // device sized); mouse coordinates and CameraController are in LOGICAL pixels.
  // Scale across so the grab regions land exactly on the drawn geometry instead
  // of being off by the device pixel ratio on a HiDPI display.
  const float dpr = static_cast<float>(devicePixelRatioF());
  return camera_->world_per_logical_pixel() / std::max(dpr, 1e-3f);
}

renderer::SectionGizmoPart ViewportWidget::hit_section_gizmo(QPoint pos) const {
  using Part = renderer::SectionGizmoPart;
  if (!display_mode_.section_enabled || !display_mode_.section_show_plane) {
    return Part::None;
  }
  if (!scene_ || !scene_->world_bounds.valid()) return Part::None;

  const scene::Aabb& bounds = scene_->world_bounds;
  const float wpp = section_world_per_device_pixel();
  const scene::vec2 cursor(static_cast<float>(pos.x()),
                           static_cast<float>(pos.y()));

  // The translate shaft gets first refusal. It is drawn inside the rings and
  // sliding the plane is the more common action, so a press in the crowded
  // middle should move the plane rather than tilt it.
  scene::vec3 a, b;
  if (renderer::section_handle_segment(display_mode_.section, bounds, wpp, a,
                                       b)) {
    bool behind_a = false, behind_b = false;
    const scene::vec2 pa = camera_->project_to_screen(a, &behind_a);
    const scene::vec2 pb = camera_->project_to_screen(b, &behind_b);
    // A handle straddling a perspective eye plane has no meaningful projection;
    // refusing the grab beats grabbing a garbage coordinate.
    if (!behind_a && !behind_b &&
        renderer::distance_to_segment(cursor, pa, pb) <= kHandleGrabPx) {
      return Part::Translate;
    }
  }

  // Then the closest live ring, as a projected polyline. Dead rings (axis on
  // the normal) are not drawn, so they must not be grabbable either.
  Part  best   = Part::None;
  float best_d = kRingGrabPx;
  for (const Part part : {Part::RotateX, Part::RotateY, Part::RotateZ}) {
    if (!renderer::section_ring_live(display_mode_.section, part)) continue;
    scene::vec3 centre, e0, e1;
    if (!renderer::section_ring_frame(display_mode_.section, bounds, part, wpp,
                                      centre, e0, e1)) {
      continue;
    }
    scene::vec2 prev{};
    bool prev_valid = false;
    for (int i = 0; i <= kRingSamples; ++i) {
      const float t = kTwoPi * static_cast<float>(i) / kRingSamples;
      bool behind = false;
      const scene::vec2 p = camera_->project_to_screen(
        renderer::section_ring_point(centre, e0, e1, t), &behind);
      // Drop only the spans that touch the eye plane, not the whole ring: in
      // perspective a ring around a nearby part can have one arc behind the eye
      // while the rest is perfectly grabbable.
      if (behind) { prev_valid = false; continue; }
      if (prev_valid) {
        const float d = renderer::distance_to_segment(cursor, prev, p);
        if (d < best_d) { best_d = d; best = part; }
      }
      prev = p;
      prev_valid = true;
    }
  }
  return best;
}

bool ViewportWidget::section_ring_angle_at_cursor(
  const renderer::SectionPlane& plane, renderer::SectionGizmoPart part,
  QPoint pos, float& out_angle) const {
  if (!scene_ || !scene_->world_bounds.valid()) return false;
  scene::vec3 centre, e0, e1;
  if (!renderer::section_ring_frame(plane, scene_->world_bounds, part,
                                    section_world_per_device_pixel(), centre,
                                    e0, e1)) {
    return false;
  }
  const ScreenRay ray = camera_->screen_ray(pos);
  return renderer::section_ring_angle_at(centre, e0, e1,
                                         renderer::section_ring_axis(part),
                                         ray.origin, ray.direction, out_angle);
}

bool ViewportWidget::section_offset_at(QPoint pos, float& out_offset) const {
  if (!scene_ || !scene_->world_bounds.valid()) return false;

  const scene::Aabb& bounds = scene_->world_bounds;
  const scene::vec3 axis   = display_mode_.section.unit_normal();
  // The axis passes through the bounds centre by definition of SectionPlane's
  // offset, so measuring `t` from there yields the offset directly.
  const scene::vec3 origin = bounds.center();

  const ScreenRay ray = camera_->screen_ray(pos);
  float t = 0.0f;
  if (!renderer::closest_point_on_axis(origin, axis, ray.origin, ray.direction,
                                       t)) {
    return false;
  }

  const float range = display_mode_.section.offset_range(bounds);
  out_offset = std::clamp(t, -range, range);
  return true;
}

void ViewportWidget::mousePressEvent(QMouseEvent* e) {
  using DM   = CameraController::DragMode;
  using Part = renderer::SectionGizmoPart;
  // The active NavigationScheme maps (button, modifiers) to a drag mode —
  // see NavigationScheme.cpp for the tables. No scheme maps a *plain* left
  // press: the section gizmo can claim it first, then picking. Until a pick
  // handler lands, an unclaimed left press clears the selection highlight.
  const DM mode = resolve_navigation(nav_scheme_, e->button(), e->modifiers());
  if (mode != DM::None) {
    camera_->begin_drag(mode, e->pos());
    camera_drag_active_ = true;
    setCursor(Qt::ClosedHandCursor);
    e->accept();
  } else if (e->button() == Qt::LeftButton) {
    // The section gizmo gets first refusal on the press, and must CONSUME it:
    // falling through to background_clicked() would clear the sidebar's
    // selection every time the user reached for the plane.
    const Part part = hit_section_gizmo(e->pos());
    if (part != Part::None) {
      section_drag_part_        = part;
      section_drag_last_pos_    = e->pos();
      section_drag_angle_       = 0.0f;
      section_drag_start_plane_ = display_mode_.section;
      section_drag_start_angle_ = 0.0f;

      if (part == Part::Translate) {
        // Remember where along the axis the cursor was pointing relative to the
        // plane, so the plane slides with the cursor instead of jumping so its
        // centre lands under the press.
        float grabbed = 0.0f;
        section_drag_grab_ = section_offset_at(e->pos(), grabbed)
          ? display_mode_.section.offset - grabbed
          : 0.0f;
      } else if (!section_ring_angle_at_cursor(section_drag_start_plane_, part,
                                               e->pos(),
                                               section_drag_start_angle_)) {
        // Edge-on ring: there is no exact grab angle, so anchor on the sampled
        // point that projects nearest the cursor. Only the screen-tangent
        // fallback reads it, and a fraction of a degree there costs nothing.
        section_drag_start_angle_ = 0.0f;
        scene::vec3 centre, e0, e1;
        if (scene_ && scene_->world_bounds.valid() &&
            renderer::section_ring_frame(section_drag_start_plane_,
                                         scene_->world_bounds, part,
                                         section_world_per_device_pixel(),
                                         centre, e0, e1)) {
          const scene::vec2 cursor(static_cast<float>(e->pos().x()),
                                   static_cast<float>(e->pos().y()));
          float best = std::numeric_limits<float>::max();
          for (int i = 0; i < kRingSamples; ++i) {
            const float t = kTwoPi * static_cast<float>(i) / kRingSamples;
            bool behind = false;
            const scene::vec2 p = camera_->project_to_screen(
              renderer::section_ring_point(centre, e0, e1, t), &behind);
            if (behind) continue;
            const float d = glm::length(cursor - p);
            if (d < best) { best = d; section_drag_start_angle_ = t; }
          }
        }
      }

      display_mode_.section_hot_part = part;
      setCursor(cursor_for_part(part, /*dragging=*/true));
      update();
      e->accept();
      return;
    }
    emit background_clicked();
    e->accept();
  } else {
    QOpenGLWidget::mousePressEvent(e);
  }
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* e) {
  using Part = renderer::SectionGizmoPart;
  // A manipulator drag owns the gesture outright, and is handled BEFORE the
  // late Alt promotion below: Alt arriving part-way through a ring drag must
  // not convert it into an orbit and throw away the plane the user is tilting.
  if (section_drag_part_ == Part::Translate) {
    float offset = 0.0f;
    // A false here means the plane normal has swung nearly parallel to the view,
    // where a pixel of cursor movement maps to an unbounded world jump. Hold the
    // current offset for this frame rather than letting the plane fly off.
    if (section_offset_at(e->pos(), offset)) {
      const float range =
        display_mode_.section.offset_range(scene_->world_bounds);
      display_mode_.section.offset =
        std::clamp(offset + section_drag_grab_, -range, range);
      update();
      emit section_plane_dragged(display_mode_.section, Part::Translate, 0.0f);
    }
    section_drag_last_pos_ = e->pos();
    e->accept();
    return;
  }

  if (section_drag_part_ != Part::None) {
    update_section_rotation(e->pos(), e->modifiers() & Qt::ShiftModifier);
    section_drag_last_pos_ = e->pos();
    e->accept();
    return;
  }

  // Late promotion: a drag whose press didn't resolve is dead by default,
  // but synthesized drags (macOS three-finger drag, BetterTouchTool) can
  // deliver the mouse-down a beat before the modifier flags land — and
  // users naturally plant fingers first, modifier second. If the held
  // button + current modifiers resolve now, start that drag from the current
  // position. A section drag has already consumed its gesture above.
  if (!camera_drag_active_) {
    using DM = CameraController::DragMode;
    Qt::MouseButton held = Qt::NoButton;
    if      (e->buttons() == Qt::LeftButton)   held = Qt::LeftButton;
    else if (e->buttons() == Qt::MiddleButton) held = Qt::MiddleButton;
    else if (e->buttons() == Qt::RightButton)  held = Qt::RightButton;
    const DM mode = resolve_navigation(nav_scheme_, held, e->modifiers());
    if (mode != DM::None) {
      camera_->begin_drag(mode, e->pos());
      camera_drag_active_ = true;
      setCursor(Qt::ClosedHandCursor);
    }
  }

  // Hover feedback, so the manipulator announces itself as grabbable before the
  // user commits to a press. Mouse tracking is on (see the constructor). Not
  // while the camera is being dragged: the rings sweep past the cursor as the
  // view turns, and the hover cursor would fight the orbit cursor all the way
  // round.
  if (display_mode_.section_enabled && !camera_drag_active_) {
    const Part hot = hit_section_gizmo(e->pos());
    if (hot != display_mode_.section_hot_part) {
      display_mode_.section_hot_part = hot;
      setCursor(cursor_for_part(hot, /*dragging=*/false));
      update();
    }
  }

  camera_->update_drag(e->pos());
  QOpenGLWidget::mouseMoveEvent(e);
}

// One rotation drag step. `snap` quantises to kRotateSnapDeg so the user can
// land on a drafting angle exactly.
//
// Two mappings, and which one runs is not a style choice. The primary one
// intersects the cursor ray with the ring's own plane and reads the angle off
// there: absolute, so the ring tracks the cursor exactly and no amount of
// dragging accumulates error. It has no answer at all when the ring is edge-on
// to the view — and that is not a corner case here, because section mode starts
// with the plane normal pointing down the view axis, which leaves the two useful
// rings exactly edge-on. So the fallback maps cursor motion onto the ring's
// projected tangent instead, which stays well-conditioned precisely where the
// intersection dies. Both write the same accumulator, so a drag that crosses
// from one regime into the other does not jump.
void ViewportWidget::update_section_rotation(QPoint pos, bool snap) {
  if (!scene_ || !scene_->world_bounds.valid()) return;
  const renderer::SectionGizmoPart part = section_drag_part_;

  float angle_now = 0.0f;
  if (section_ring_angle_at_cursor(section_drag_start_plane_, part, pos,
                                   angle_now)) {
    section_drag_angle_ =
      renderer::wrap_angle(angle_now - section_drag_start_angle_);
  } else {
    scene::vec3 centre, e0, e1;
    if (!renderer::section_ring_frame(section_drag_start_plane_,
                                      scene_->world_bounds, part,
                                      section_world_per_device_pixel(), centre,
                                      e0, e1)) {
      return;
    }
    // Tangent at the point currently under the grab, by finite difference in
    // screen space: dp/dangle, in pixels per radian.
    const float at = section_drag_start_angle_ + section_drag_angle_;
    bool behind_0 = false, behind_1 = false;
    const scene::vec2 p0 = camera_->project_to_screen(
      renderer::section_ring_point(centre, e0, e1, at), &behind_0);
    const scene::vec2 p1 = camera_->project_to_screen(
      renderer::section_ring_point(centre, e0, e1, at + kTangentEps), &behind_1);
    if (behind_0 || behind_1) return;
    const scene::vec2 tangent = p1 - p0;
    const float len2 = glm::dot(tangent, tangent);
    // The tangent vanishes at the two ends of an edge-on ring, where it points
    // straight at the camera. Hold rather than divide by ~0; a few pixels of
    // cursor travel gets the grab off the turning point.
    if (len2 < 1e-8f) return;
    const scene::vec2 motion(
      static_cast<float>(pos.x() - section_drag_last_pos_.x()),
      static_cast<float>(pos.y() - section_drag_last_pos_.y()));
    section_drag_angle_ += kTangentEps * glm::dot(motion, tangent) / len2;
  }

  float applied = section_drag_angle_;
  if (snap) {
    const float step = kRotateSnapDeg * kPi / 180.0f;
    applied = std::round(applied / step) * step;
  }
  display_mode_.section = renderer::section_plane_rotated(
    section_drag_start_plane_, scene_->world_bounds,
    renderer::section_ring_axis(part), applied);
  update();
  emit section_plane_dragged(display_mode_.section, part,
                             applied * 180.0f / kPi);
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent* e) {
  using Part = renderer::SectionGizmoPart;
  if (section_drag_part_ != Part::None && e->button() == Qt::LeftButton) {
    section_drag_part_ = Part::None;
    display_mode_.section_hot_part = hit_section_gizmo(e->pos());
    setCursor(cursor_for_part(display_mode_.section_hot_part,
                              /*dragging=*/false));
    update();
    emit section_drag_finished();
    e->accept();
    return;
  }
  camera_->end_drag();
  camera_drag_active_ = false;
  setCursor(Qt::ArrowCursor);
  QOpenGLWidget::mouseReleaseEvent(e);
}

void ViewportWidget::wheelEvent(QWheelEvent* e) {
  // Forward widget-local cursor position so the controller can anchor zoom
  // to the point under the pointer (the same coordinate space the controller
  // sees via set_viewport()).
  camera_->wheel(e->position().toPoint(), e->angleDelta().y());
  e->accept();
}

} // namespace cadly::ui

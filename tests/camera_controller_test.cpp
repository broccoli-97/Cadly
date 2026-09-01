// Zoom policy tests for CameraController: deep zoom must magnify the model,
// never slice it open ("inside the model" cutaway). Orthographic achieves
// this with unlimited zoom plus a clip slab fitted around the whole model;
// perspective by keeping the eye outside the scene's bounding sphere.

#include "cadly/ui/CameraController.h"
#include "cadly/ui/ViewportWidget.h"

#include <QMouseEvent>
#include <QTest>

#include <cmath>

namespace cadly::ui {

namespace {

constexpr float kHalf = 1.0f;  // test model: cube spanning [-1, 1]^3

// True if every corner of the test cube lies inside the camera's clip slab,
// i.e. nothing of the model is cut by the near or far plane.
bool cube_inside_clip_slab(const scene::Camera& cam) {
  for (int i = 0; i < 8; ++i) {
    const scene::vec3 corner{(i & 1) ? kHalf : -kHalf,
                             (i & 2) ? kHalf : -kHalf,
                             (i & 4) ? kHalf : -kHalf};
    const float depth = glm::dot(corner - cam.position(), cam.forward());
    if (depth < cam.near_z || depth > cam.far_z) return false;
  }
  return true;
}

void prepare_section_viewport(ViewportWidget& viewport) {
  viewport.resize(640, 480);
  auto scn = std::make_shared<scene::Scene>();
  scn->world_bounds.expand({-kHalf, -kHalf, -kHalf});
  scn->world_bounds.expand({kHalf, kHalf, kHalf});
  viewport.set_scene(scn);
  viewport.camera_controller()->set_viewport(640, 480);
  renderer::DisplayMode mode;
  mode.section_enabled = true;
  mode.section.normal = {1.0f, 0.0f, 0.0f};
  viewport.set_display_mode(mode);
}

void send_mouse(ViewportWidget& viewport, QEvent::Type type, QPoint pos,
                 Qt::MouseButton button, Qt::MouseButtons buttons,
                 Qt::KeyboardModifiers mods) {
  QMouseEvent event(type, QPointF(pos), QPointF(pos), button, buttons, mods);
  QCoreApplication::sendEvent(&viewport, &event);
}

}  // namespace

class CameraControllerTest : public QObject {
  Q_OBJECT

private slots:
  void ortho_deep_zoom_magnifies_without_slicing();
  void perspective_zoom_stops_outside_model();
  void perspective_toggle_moves_eye_outside();
  void screen_ray_round_trips_in_both_projections();
  void project_reports_points_behind_the_eye();
  void section_viewport_respects_navigation_scheme();
  void section_drag_keeps_priority_over_late_modifiers();
};

void CameraControllerTest::ortho_deep_zoom_magnifies_without_slicing() {
  CameraController ctrl;
  ctrl.set_viewport(1000, 800);
  ctrl.frame_bounds({-kHalf, -kHalf, -kHalf}, {kHalf, kHalf, kHalf});
  QVERIFY(ctrl.camera().projection_mode == scene::Projection::Orthographic);

  // Wheel far past the model surface, anchored off-centre the way a user
  // zooming onto a feature does. Zoom must keep magnifying (distance keeps
  // shrinking) and the model must stay wholly inside the clip slab at every
  // step — the near plane never slices it, no matter how deep the zoom.
  const float start_distance = ctrl.camera().distance;
  for (int i = 0; i < 100; ++i) {
    ctrl.wheel(QPoint(650, 300), 240);
    QVERIFY(cube_inside_clip_slab(ctrl.camera()));
  }
  QVERIFY(ctrl.camera().distance < start_distance * 1e-3f);
}

void CameraControllerTest::perspective_zoom_stops_outside_model() {
  CameraController ctrl;
  ctrl.set_viewport(1000, 800);
  ctrl.frame_bounds({-kHalf, -kHalf, -kHalf}, {kHalf, kHalf, kHalf});
  ctrl.set_projection(scene::Projection::Perspective);

  // Zooming in must run up against the bounding sphere and stop there: the
  // eye stays outside the model and the near plane keeps the whole cube in
  // view instead of cutting it open.
  const float radius = std::sqrt(3.0f) * kHalf;
  for (int i = 0; i < 100; ++i) {
    ctrl.wheel(QPoint(500, 400), 240);
    QVERIFY(cube_inside_clip_slab(ctrl.camera()));
  }
  const float eye_to_center = glm::length(ctrl.camera().position());
  QVERIFY(eye_to_center >= radius);

  // Zooming back out from the clamp must still work.
  const float clamped = ctrl.camera().distance;
  ctrl.wheel(QPoint(500, 400), -240);
  QVERIFY(ctrl.camera().distance > clamped);
}

void CameraControllerTest::perspective_toggle_moves_eye_outside() {
  CameraController ctrl;
  ctrl.set_viewport(1000, 800);
  ctrl.frame_bounds({-kHalf, -kHalf, -kHalf}, {kHalf, kHalf, kHalf});

  // Park the eye deep inside the model with ortho zoom (legal there), then
  // switch to perspective: the controller must pop the eye back outside the
  // bounding sphere so the first perspective frame is not a cutaway.
  for (int i = 0; i < 100; ++i) ctrl.wheel(QPoint(500, 400), 240);
  QVERIFY(glm::length(ctrl.camera().position()) < std::sqrt(3.0f) * kHalf);

  ctrl.set_projection(scene::Projection::Perspective);
  QVERIFY(glm::length(ctrl.camera().position()) >= std::sqrt(3.0f) * kHalf);
  QVERIFY(cube_inside_clip_slab(ctrl.camera()));
}

// screen_ray / project_to_screen back the section-plane drag handle: the ray
// turns a cursor position into a plane offset, and the projection turns the
// handle's world position into the pixels the hit-test measures against. They
// must be exact inverses, in BOTH projection modes — ortho is the default for
// CAD, and it is the mode where a perspective-only formulation quietly breaks
// (there is no meaningful eye point to build a ray from).
void CameraControllerTest::screen_ray_round_trips_in_both_projections() {
  for (const auto projection : {scene::Projection::Orthographic,
                                scene::Projection::Perspective}) {
    CameraController ctrl;
    ctrl.set_viewport(1000, 800);
    ctrl.frame_bounds({-kHalf, -kHalf, -kHalf}, {kHalf, kHalf, kHalf});
    ctrl.set_projection(projection);

    // A ray through the viewport centre must run along the view direction and,
    // in both modes, pass through the camera target.
    const ScreenRay centre = ctrl.screen_ray(QPoint(500, 400));
    QVERIFY(glm::length(centre.direction) == 1.0f ||
            std::fabs(glm::length(centre.direction) - 1.0f) < 1e-4f);
    QVERIFY(glm::dot(centre.direction, ctrl.camera().forward()) > 0.999f);
    const scene::vec3 to_target = ctrl.camera().target - centre.origin;
    // Distance from the target to the ray line, via the cross product.
    QVERIFY(glm::length(glm::cross(to_target, centre.direction)) < 1e-3f);

    // Round trip: walk a known distance down a ray through an off-centre pixel,
    // project the point back, and land on the pixel we started from.
    for (const QPoint pixel : {QPoint(320, 260), QPoint(760, 610)}) {
      const ScreenRay ray = ctrl.screen_ray(pixel);
      const scene::vec3 world =
        ray.origin + ray.direction * ctrl.camera().distance;
      bool behind = true;
      const scene::vec2 back = ctrl.project_to_screen(world, &behind);
      QVERIFY(!behind);
      QVERIFY(std::fabs(back.x - static_cast<float>(pixel.x())) < 0.5f);
      QVERIFY(std::fabs(back.y - static_cast<float>(pixel.y())) < 0.5f);
    }

    // The scale manipulators size themselves with must be positive and finite,
    // or a handle collapses to nothing (or explodes) at some zoom level.
    QVERIFY(ctrl.world_per_logical_pixel() > 0.0f);
    QVERIFY(std::isfinite(ctrl.world_per_logical_pixel()));
  }
}

// A point behind a perspective eye has no meaningful projection. The hit-test
// must be told so rather than handed a plausible-looking coordinate, or a
// handle behind the camera becomes grabbable somewhere on screen.
void CameraControllerTest::project_reports_points_behind_the_eye() {
  CameraController ctrl;
  ctrl.set_viewport(1000, 800);
  ctrl.frame_bounds({-kHalf, -kHalf, -kHalf}, {kHalf, kHalf, kHalf});
  ctrl.set_projection(scene::Projection::Perspective);

  const scene::vec3 behind_eye =
    ctrl.camera().position() - ctrl.camera().forward() * 5.0f;
  bool behind = false;
  ctrl.project_to_screen(behind_eye, &behind);
  QVERIFY(behind);

  bool in_front = true;
  ctrl.project_to_screen(ctrl.camera().target, &in_front);
  QVERIFY(!in_front);
}

void CameraControllerTest::section_viewport_respects_navigation_scheme() {
  struct Gesture {
    NavigationScheme scheme;
    Qt::MouseButton button;
    Qt::KeyboardModifiers modifiers;
    bool orbits;
  };
  for (const Gesture& gesture : {
         Gesture{NavigationScheme::Blender, Qt::MiddleButton,
                  Qt::NoModifier, true},
         Gesture{NavigationScheme::Fusion360, Qt::MiddleButton,
                  Qt::ShiftModifier, true},
         Gesture{NavigationScheme::Maya, Qt::RightButton,
                  Qt::AltModifier, false}}) {
    ViewportWidget viewport;
    prepare_section_viewport(viewport);
    viewport.set_navigation_scheme(gesture.scheme);
    const scene::Camera before = viewport.camera_controller()->camera();
    send_mouse(viewport, QEvent::MouseButtonPress, {320, 240},
                 gesture.button, gesture.button, gesture.modifiers);
    send_mouse(viewport, QEvent::MouseMove, {360, 260}, Qt::NoButton,
                 gesture.button, gesture.modifiers);
    send_mouse(viewport, QEvent::MouseButtonRelease, {360, 260},
                 gesture.button, Qt::NoButton, gesture.modifiers);
    const scene::Camera& after = viewport.camera_controller()->camera();
    if (gesture.orbits) {
      QVERIFY(glm::length(after.forward() - before.forward()) > 1e-3f);
      QCOMPARE(after.distance, before.distance);
    } else {
      QVERIFY(std::abs(after.distance - before.distance) > 1e-3f);
      QVERIFY(glm::length(after.forward() - before.forward()) < 1e-6f);
    }
    QCOMPARE(viewport.display_mode().section.offset, 0.0f);
  }
}

void CameraControllerTest::section_drag_keeps_priority_over_late_modifiers() {
  ViewportWidget viewport;
  prepare_section_viewport(viewport);
  viewport.set_navigation_scheme(NavigationScheme::Maya);
  const scene::Camera before = viewport.camera_controller()->camera();
  int plane_moves = 0;
  int background_clicks = 0;
  connect(&viewport, &ViewportWidget::section_plane_dragged, this,
          [&]() { ++plane_moves; });
  connect(&viewport, &ViewportWidget::background_clicked, this,
          [&]() { ++background_clicks; });

  send_mouse(viewport, QEvent::MouseButtonPress, {320, 240},
               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  send_mouse(viewport, QEvent::MouseMove, {350, 250}, Qt::NoButton,
               Qt::LeftButton, Qt::AltModifier);
  send_mouse(viewport, QEvent::MouseMove, {360, 260}, Qt::NoButton,
               Qt::LeftButton, Qt::AltModifier);
  send_mouse(viewport, QEvent::MouseButtonRelease, {360, 260},
               Qt::LeftButton, Qt::NoButton, Qt::AltModifier);

  QVERIFY(plane_moves > 0);
  QCOMPARE(background_clicks, 0);
  QVERIFY(std::abs(viewport.display_mode().section.offset) > 1e-3f);
  QVERIFY(!viewport.display_mode().show_rotation_pivot);
  const scene::Camera& after = viewport.camera_controller()->camera();
  QVERIFY(glm::length(after.forward() - before.forward()) < 1e-6f);
  QVERIFY(glm::length(after.target - before.target) < 1e-6f);
  QCOMPARE(after.distance, before.distance);
}

}  // namespace cadly::ui

QTEST_MAIN(cadly::ui::CameraControllerTest)

#include "camera_controller_test.moc"

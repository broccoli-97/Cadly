// Zoom policy tests for CameraController: deep zoom must magnify the model,
// never slice it open ("inside the model" cutaway). Orthographic achieves
// this with unlimited zoom plus a clip slab fitted around the whole model;
// perspective by keeping the eye outside the scene's bounding sphere.

#include "cadly/ui/CameraController.h"

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

}  // namespace

class CameraControllerTest : public QObject {
  Q_OBJECT

private slots:
  void ortho_deep_zoom_magnifies_without_slicing();
  void perspective_zoom_stops_outside_model();
  void perspective_toggle_moves_eye_outside();
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

}  // namespace cadly::ui

QTEST_MAIN(cadly::ui::CameraControllerTest)

#include "camera_controller_test.moc"

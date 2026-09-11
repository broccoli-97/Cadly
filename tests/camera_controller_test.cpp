#include "cadly/ui/CameraController.h"
#include "cadly/input/Navigation.h"
#include "TestChecks.h"

#include <QCoreApplication>

#include <cmath>
#include <limits>

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


void ortho_deep_zoom_magnifies_without_slicing() {
  CameraController ctrl;
  ctrl.set_viewport(1000, 800);
  ctrl.frame_bounds({-kHalf, -kHalf, -kHalf}, {kHalf, kHalf, kHalf});
  CHECK(ctrl.camera().projection_mode == scene::Projection::Orthographic);

  // Wheel far past the model surface, anchored off-centre the way a user
  // zooming onto a feature does. Zoom must keep magnifying (distance keeps
  // shrinking) and the model must stay wholly inside the clip slab at every
  // step — the near plane never slices it, no matter how deep the zoom.
  const float start_distance = ctrl.camera().distance;
  for (int i = 0; i < 100; ++i) {
    ctrl.apply_navigation(input::scroll_zoom({650, 300}, 240));
    CHECK(cube_inside_clip_slab(ctrl.camera()));
  }
  CHECK(ctrl.camera().distance < start_distance * 1e-3f);
}

void perspective_zoom_stops_outside_model() {
  CameraController ctrl;
  ctrl.set_viewport(1000, 800);
  ctrl.frame_bounds({-kHalf, -kHalf, -kHalf}, {kHalf, kHalf, kHalf});
  ctrl.set_projection(scene::Projection::Perspective);

  // Zooming in must run up against the bounding sphere and stop there: the
  // eye stays outside the model and the near plane keeps the whole cube in
  // view instead of cutting it open.
  const float radius = std::sqrt(3.0f) * kHalf;
  for (int i = 0; i < 100; ++i) {
    ctrl.apply_navigation(input::scroll_zoom({500, 400}, 240));
    CHECK(cube_inside_clip_slab(ctrl.camera()));
  }
  const float eye_to_center = glm::length(ctrl.camera().position());
  CHECK(eye_to_center >= radius);

  // Zooming back out from the clamp must still work.
  const float clamped = ctrl.camera().distance;
  ctrl.apply_navigation(input::scroll_zoom({500, 400}, -240));
  CHECK(ctrl.camera().distance > clamped);
}

void perspective_toggle_moves_eye_outside() {
  CameraController ctrl;
  ctrl.set_viewport(1000, 800);
  ctrl.frame_bounds({-kHalf, -kHalf, -kHalf}, {kHalf, kHalf, kHalf});

  // Park the eye deep inside the model with ortho zoom (legal there), then
  // switch to perspective: the controller must pop the eye back outside the
  // bounding sphere so the first perspective frame is not a cutaway.
  for (int i = 0; i < 100; ++i) ctrl.apply_navigation(input::scroll_zoom({500, 400}, 240));
  CHECK(glm::length(ctrl.camera().position()) < std::sqrt(3.0f) * kHalf);

  ctrl.set_projection(scene::Projection::Perspective);
  CHECK(glm::length(ctrl.camera().position()) >= std::sqrt(3.0f) * kHalf);
  CHECK(cube_inside_clip_slab(ctrl.camera()));
}

// screen_ray / project_to_screen back the section-plane drag handle: the ray
// turns a cursor position into a plane offset, and the projection turns the
// handle's world position into the pixels the hit-test measures against. They
// must be exact inverses, in BOTH projection modes — ortho is the default for
// CAD, and it is the mode where a perspective-only formulation quietly breaks
// (there is no meaningful eye point to build a ray from).
void screen_ray_round_trips_in_both_projections() {
  for (const auto projection : {scene::Projection::Orthographic,
                                scene::Projection::Perspective}) {
    CameraController ctrl;
    ctrl.set_viewport(1000, 800);
    ctrl.frame_bounds({-kHalf, -kHalf, -kHalf}, {kHalf, kHalf, kHalf});
    ctrl.set_projection(projection);

    // A ray through the viewport centre must run along the view direction and,
    // in both modes, pass through the camera target.
    const ScreenRay centre = ctrl.screen_ray(QPoint(500, 400));
    CHECK(glm::length(centre.direction) == 1.0f ||
            std::fabs(glm::length(centre.direction) - 1.0f) < 1e-4f);
    CHECK(glm::dot(centre.direction, ctrl.camera().forward()) > 0.999f);
    const scene::vec3 to_target = ctrl.camera().target - centre.origin;
    // Distance from the target to the ray line, via the cross product.
    CHECK(glm::length(glm::cross(to_target, centre.direction)) < 1e-3f);

    // Round trip: walk a known distance down a ray through an off-centre pixel,
    // project the point back, and land on the pixel we started from.
    for (const QPoint pixel : {QPoint(320, 260), QPoint(760, 610)}) {
      const ScreenRay ray = ctrl.screen_ray(pixel);
      const scene::vec3 world =
        ray.origin + ray.direction * ctrl.camera().distance;
      bool behind = true;
      const scene::vec2 back = ctrl.project_to_screen(world, &behind);
      CHECK(!behind);
      CHECK(std::fabs(back.x - static_cast<float>(pixel.x())) < 0.5f);
      CHECK(std::fabs(back.y - static_cast<float>(pixel.y())) < 0.5f);
    }

    // The scale manipulators size themselves with must be positive and finite,
    // or a handle collapses to nothing (or explodes) at some zoom level.
    CHECK(ctrl.world_per_logical_pixel() > 0.0f);
    CHECK(std::isfinite(ctrl.world_per_logical_pixel()));
  }
}

// A point behind a perspective eye has no meaningful projection. The hit-test
// must be told so rather than handed a plausible-looking coordinate, or a
// handle behind the camera becomes grabbable somewhere on screen.
void project_reports_points_behind_the_eye() {
  CameraController ctrl;
  ctrl.set_viewport(1000, 800);
  ctrl.frame_bounds({-kHalf, -kHalf, -kHalf}, {kHalf, kHalf, kHalf});
  ctrl.set_projection(scene::Projection::Perspective);

  const scene::vec3 behind_eye =
    ctrl.camera().position() - ctrl.camera().forward() * 5.0f;
  bool behind = false;
  ctrl.project_to_screen(behind_eye, &behind);
  CHECK(behind);

  bool in_front = true;
  ctrl.project_to_screen(ctrl.camera().target, &in_front);
  CHECK(!in_front);
}


void cursor_zoom_keeps_its_anchor_in_both_projections() {
  for (const auto projection : {scene::Projection::Orthographic, scene::Projection::Perspective}) {
    CameraController ctrl;
    ctrl.set_viewport(1000, 800);
    ctrl.frame_bounds({-kHalf, -kHalf, -kHalf}, {kHalf, kHalf, kHalf});
    ctrl.set_projection(projection);
    const QPoint pixel(710, 290);
    const auto ray = ctrl.screen_ray(pixel);
    const auto forward = ctrl.camera().forward();
    const float t = glm::dot(ctrl.camera().target - ray.origin, forward) /
                    glm::dot(ray.direction, forward);
    const auto anchor = ray.origin + ray.direction * t;
    const auto before = ctrl.camera();
    for (const int delta : {-120, 120}) {
      ctrl.apply_navigation(input::scroll_zoom({pixel.x(), pixel.y()}, delta));
      const auto screen = ctrl.project_to_screen(anchor);
      CHECK(std::abs(screen.x - pixel.x()) < 0.002f);
      CHECK(std::abs(screen.y - pixel.y()) < 0.002f);
      CHECK(cube_inside_clip_slab(ctrl.camera()));
    }
    CHECK(std::abs(ctrl.camera().distance - before.distance) < 1e-5f);
    CHECK(glm::length(ctrl.camera().target - before.target) < 1e-5f);
  }
}

void pan_and_dolly_execute_resolved_motion() {
  CameraController ctrl;
  ctrl.frame_bounds({-kHalf, -kHalf, -kHalf}, {kHalf, kHalf, kHalf});
  const auto before = ctrl.camera();
  ctrl.apply_navigation(input::drag_motion(input::NavigationAction::Pan, {0, 0}, {40, 20}, {}));
  const auto expected = before.target - before.right() * (before.distance * 0.1f) +
                                        before.up() * (before.distance * 0.05f);
  CHECK(glm::length(ctrl.camera().target - expected) < 1e-5f);
  CHECK(ctrl.camera().distance == before.distance);
  CHECK(glm::length(ctrl.camera().forward() - before.forward()) < 1e-6f);
  CHECK(cube_inside_clip_slab(ctrl.camera()));
  ctrl.apply_navigation(input::drag_motion(input::NavigationAction::Dolly, {}, {0, 20}, {}));
  CHECK(std::abs(ctrl.camera().distance - before.distance * 1.05f) < 1e-5f);
  CHECK(glm::length(ctrl.camera().target - expected) < 1e-5f);
  CHECK(cube_inside_clip_slab(ctrl.camera()));
}

class FixedPivotResolver final : public RotationPivotResolver {
public:
  FixedPivotResolver(int& calls, QPoint& last_point, scene::vec3 pivot)
    : calls_(calls), last_point_(last_point), pivot_(pivot) {}
  RotationPivot resolve(const scene::Camera&, QPoint pos) const override {
    ++calls_;
    last_point_ = pos;
    return {pivot_};
  }
private:
  int& calls_;
  QPoint& last_point_;
  scene::vec3 pivot_;
};

void orbit_resolves_one_pivot_and_balances_visibility() {
  CameraController ctrl;
  int resolutions = 0, shown = 0, hidden = 0;
  QPoint resolved_at;
  const scene::vec3 pivot(2.0f, -1.0f, 3.0f);
  ctrl.set_rotation_pivot_resolver(
    std::make_unique<FixedPivotResolver>(resolutions, resolved_at, pivot));
  QObject::connect(&ctrl, &CameraController::rotation_pivot_visibility_changed,
                   [&](scene::vec3 where, bool visible) {
    CHECK(glm::length(where - ctrl.rotation_pivot()) < 1e-6f);
    if (visible) ++shown;
    else ++hidden;
  });
  const input::NavigationCommand start{input::CommandType::BeginOrbit, {120, 80}};
  ctrl.apply_navigation(start);
  ctrl.apply_navigation(start);  // repeated begin must not resolve again
  CHECK(ctrl.is_rotating() && shown == 1 && hidden == 0);
  CHECK(resolutions == 1 && resolved_at == QPoint(120, 80));
  CHECK(glm::length(ctrl.rotation_pivot() - pivot) < 1e-6f);
  const auto before = ctrl.camera();
  auto expected = before;
  expected.orbit(-0.1f, 0.2f, pivot);
  ctrl.apply_navigation({input::CommandType::OrbitFree, {}, -0.1f, 0.2f});
  CHECK(glm::length(ctrl.camera().target - expected.target) < 1e-5f);
  CHECK(glm::length(ctrl.camera().position() - expected.position()) < 1e-5f);
  CHECK(ctrl.camera().distance == before.distance && resolutions == 1);
  ctrl.apply_navigation({input::CommandType::EndOrbit});
  ctrl.apply_navigation({input::CommandType::EndOrbit});
  CHECK(!ctrl.is_rotating() && shown == 1 && hidden == 1);
  const auto after = ctrl.camera();
  ctrl.apply_navigation({input::CommandType::OrbitFree, {}, 1.0f, 1.0f});
  CHECK(glm::length(ctrl.camera().forward() - after.forward()) < 1e-6f);
  ctrl.set_rotation_pivot_resolver(nullptr);
  ctrl.apply_navigation(start);
  CHECK(resolutions == 1 && shown == 2);
  CHECK(glm::length(ctrl.rotation_pivot() - ctrl.camera().target) < 1e-6f);
  ctrl.apply_navigation({input::CommandType::EndOrbit});
  CHECK(hidden == 2);
}

void free_and_turntable_keep_their_distinct_orbit_contracts() {
  for (const auto type : {input::CommandType::OrbitFree, input::CommandType::OrbitTurntable}) {
    CameraController ctrl;
    ctrl.set_view(0.0f, 0.0f);
    const float distance = ctrl.camera().distance;
    ctrl.apply_navigation({input::CommandType::BeginOrbit});
    for (int i = 0; i < 100; ++i) {
      ctrl.apply_navigation({type, {}, 0.0f, glm::radians(1.0f)});
    }
    CHECK(std::abs(glm::length(ctrl.camera().orientation) - 1.0f) < 1e-5f);
    CHECK(ctrl.camera().distance == distance);
    if (type == input::CommandType::OrbitFree) {
      CHECK(ctrl.camera().up().y < 0.0f);  // free orbit passes through the pole
    } else {
      const float elevation = glm::degrees(std::asin(ctrl.camera().forward().y));
      CHECK(elevation >= 89.0f && elevation <= 89.6f);
      for (int i = 0; i < 50; ++i) {
        ctrl.apply_navigation({type, {}, glm::radians(7.0f), glm::radians(0.5f)});
      }
      CHECK(std::abs(ctrl.camera().right().y) < 1e-4f);
    }
    ctrl.apply_navigation({input::CommandType::EndOrbit});
    ctrl.set_view(0.0f, 0.0f);
    CHECK(ctrl.camera().up().y > 0.9999f);
  }
}

void restore_and_standard_views_preserve_document_state() {
  CameraController ctrl;
  ctrl.set_viewport(1200, 800);
  scene::Camera saved;
  saved.target = {3.0f, 1.0f, -2.0f};
  saved.distance = 25.0f;
  saved.aspect = 1.0f;
  saved.projection_mode = scene::Projection::Perspective;
  ctrl.restore_camera(saved, {-kHalf, -kHalf, -kHalf}, {kHalf, kHalf, kHalf});
  CHECK(glm::length(ctrl.camera().target - saved.target) < 1e-6f);
  CHECK(ctrl.camera().distance == saved.distance);
  CHECK(ctrl.camera().aspect == 1.5f);
  CHECK(ctrl.camera().projection_mode == scene::Projection::Perspective);
  CHECK(cube_inside_clip_slab(ctrl.camera()));
  ctrl.set_view(90.0f, -20.0f);
  CHECK(glm::length(ctrl.camera().target - saved.target) < 1e-6f);
  CHECK(ctrl.camera().distance == saved.distance);
  ctrl.set_viewport(0, -1);
  CHECK(ctrl.camera().aspect == 1.0f);
  CHECK(std::isfinite(ctrl.world_per_logical_pixel()));
  CHECK(ctrl.world_per_logical_pixel() > 0.0f);
}

void invalid_motion_and_noops_do_not_corrupt_the_camera() {
  CameraController ctrl;
  int changes = 0;
  QObject::connect(&ctrl, &CameraController::changed, [&]() { ++changes; });
  const auto before = ctrl.camera();
  ctrl.apply_navigation({});
  for (const float factor : {-1.0f, std::numeric_limits<float>::infinity(),
                             std::numeric_limits<float>::quiet_NaN()}) {
    ctrl.apply_navigation({input::CommandType::Zoom, {}, 0.0f, 0.0f, factor});
    ctrl.apply_navigation({input::CommandType::ZoomAtCursor, {}, 0.0f, 0.0f, factor});
  }
  ctrl.apply_navigation({input::CommandType::Pan, {}, std::numeric_limits<float>::infinity(), 0.0f});
  CHECK(changes == 0);
  CHECK(ctrl.camera().distance == before.distance);
  CHECK(glm::length(ctrl.camera().target - before.target) < 1e-6f);
}

} // namespace

int run_camera_tests() {
  ortho_deep_zoom_magnifies_without_slicing();
  perspective_zoom_stops_outside_model();
  perspective_toggle_moves_eye_outside();
  screen_ray_round_trips_in_both_projections();
  project_reports_points_behind_the_eye();
  cursor_zoom_keeps_its_anchor_in_both_projections();
  pan_and_dolly_execute_resolved_motion();
  orbit_resolves_one_pivot_and_balances_visibility();
  free_and_turntable_keep_their_distinct_orbit_contracts();
  restore_and_standard_views_preserve_document_state();
  invalid_motion_and_noops_do_not_corrupt_the_camera();
  return tests::report();
}
} // namespace cadly::ui

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  return cadly::ui::run_camera_tests();
}

// Tiny smoke test — verifies that the renderer-free scene math and importer
// registry are wired together. Real importer/triangulation tests need sample
// STEP files; those land in the test corpus once we collect a public set.

#include "cadly/cad/ImporterRegistry.h"
#include "cadly/cad/TessellationPolicy.h"
#include "cadly/cad/XcafSession.h"
#include "cadly/renderer/RenderTypes.h"
#include "cadly/renderer/SectionPlane.h"
#include "cadly/scene/Aabb.h"
#include "cadly/scene/Camera.h"
#include "cadly/scene/Scene.h"
#include "cadly/scene/Transform.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>

namespace s = cadly::scene;
namespace c = cadly::cad;
namespace r = cadly::renderer;

static int g_failures = 0;
#define CHECK(cond) do {                                          \
    if (!(cond)) {                                                \
      std::fprintf(stderr, "FAIL %s:%d  %s\n",                     \
                   __FILE__, __LINE__, #cond);                    \
      ++g_failures;                                               \
    }                                                             \
  } while (0)

static void test_aabb() {
  s::Aabb b = s::Aabb::empty();
  CHECK(!b.valid());
  b.expand({1.0f, 2.0f, 3.0f});
  b.expand({-1.0f, 5.0f, 0.0f});
  CHECK(b.valid());
  CHECK(b.min.x == -1.0f);
  CHECK(b.max.y ==  5.0f);
  auto t = b.transformed(glm::translate(glm::mat4(1.0f), glm::vec3(10.0f)));
  CHECK(t.min.x == 9.0f);
}

static void test_transform_roundtrip() {
  s::Transform t;
  t.translation = {1.0f, 2.0f, 3.0f};
  t.scale       = {2.0f, 0.5f, 1.5f};
  const glm::mat4 m = t.to_matrix();
  s::Transform back = s::Transform::from_matrix(m);
  CHECK(std::fabs(back.translation.x - 1.0f) < 1e-5f);
  CHECK(std::fabs(back.scale.y       - 0.5f) < 1e-5f);
}

static void test_scene_hierarchy() {
  s::Scene scn;
  auto mesh = std::make_shared<s::Mesh>();
  mesh->bounds.expand({-1.0f, -1.0f, -1.0f});
  mesh->bounds.expand({ 1.0f,  1.0f,  1.0f});
  auto mesh_idx = scn.add_mesh(mesh);

  s::Node root; root.name = "root"; root.mesh_index = mesh_idx;
  const auto root_idx = scn.add_node(std::move(root));

  s::Node child; child.name = "child"; child.parent = root_idx;
  child.mesh_index = mesh_idx;
  child.local.translation = {5.0f, 0.0f, 0.0f};
  const auto child_idx = scn.add_node(std::move(child));
  scn.nodes[root_idx].children.push_back(child_idx);

  scn.update_transforms();
  CHECK(scn.world_bounds.valid());
  CHECK(scn.world_bounds.min.x <= -1.0f);
  CHECK(scn.world_bounds.max.x >=  6.0f);
}

static void test_camera_fit() {
  s::Camera c;
  c.aspect = 16.0f / 9.0f;
  c.frame_bounds({-2.0f, -2.0f, -2.0f}, {2.0f, 2.0f, 2.0f});
  CHECK(c.distance > 0.0f);
  CHECK(c.far_z > c.near_z);
}

// Screen-space ("free orbit") tumble: the drag response must be identical in
// every orientation. A pure horizontal step swings forward() toward -right()
// by sin(step); a pure vertical step swings it toward +up() — with no stall
// at the poles and no orientation where the response reverses (the failure
// modes of the turntable schemes orbit() replaces).
static void test_camera_orbit_screen_space() {
  s::Camera c;
  c.target   = {0.0f, 0.0f, 0.0f};
  c.distance = 5.0f;

  const float step = glm::radians(10.0f);
  const s::quat test_orientations[] = {
    s::quat(1.0f, 0.0f, 0.0f, 0.0f),                                  // upright
    glm::angleAxis(glm::radians(40.0f),   s::vec3(0.0f, 1.0f, 0.0f)) *
      glm::angleAxis(glm::radians(-30.0f), s::vec3(1.0f, 0.0f, 0.0f)), // oblique
    glm::angleAxis(glm::radians(-90.0f),  s::vec3(1.0f, 0.0f, 0.0f)),  // pole
    glm::angleAxis(glm::radians(-170.0f), s::vec3(1.0f, 0.0f, 0.0f)),  // upside down
    glm::angleAxis(glm::radians(25.0f),   s::vec3(0.0f, 0.0f, 1.0f)),  // rolled
  };
  for (const s::quat& q : test_orientations) {
    c.orientation = q;
    const s::vec3 right = c.right();
    const s::vec3 up    = c.up();
    c.orbit(step, 0.0f, c.target);
    CHECK(std::fabs(glm::dot(c.forward(), right) + std::sin(step)) < 1e-4f);
    c.orientation = q;
    c.orbit(0.0f, step, c.target);
    CHECK(std::fabs(glm::dot(c.forward(), up) - std::sin(step)) < 1e-4f);
  }

  // A continuous vertical drag tumbles straight over the top (a pitch clamp
  // would stall at 90°), preserving distance and the pivot == target.
  c.set_orientation_yaw_pitch(0.0f, 0.0f);
  for (int i = 0; i < 100; ++i) c.orbit(0.0f, glm::radians(1.7f), c.target);
  CHECK(c.up().y < 0.0f);
  CHECK(std::fabs(c.distance - 5.0f) < 1e-4f);
  CHECK(glm::length(c.target) < 1e-4f);

  // A full 360° vertical tumble comes back to the starting view.
  c.set_orientation_yaw_pitch(0.0f, 0.0f);
  const s::vec3 start_fwd = c.forward();
  for (int i = 0; i < 100; ++i) c.orbit(0.0f, glm::radians(3.6f), c.target);
  CHECK(glm::length(c.forward() - start_fwd) < 1e-3f);
  CHECK(std::fabs(c.up().y - 1.0f) < 1e-3f);
}

static void test_display_mode_defaults() {
  r::DisplayMode mode;
  CHECK(!mode.wireframe);
  CHECK(!mode.hidden_line);
  CHECK(mode.show_edges);
  CHECK(!mode.show_triangle_mesh);
  CHECK(!mode.section_enabled);
  CHECK(mode.section.offset == 0.0f);
  CHECK(mode.section_hot_part == r::SectionGizmoPart::None);
}

// The plane equation's sign convention IS the feature's contract: the renderer
// keeps `dot(eq, vec4(p,1)) >= 0` and caps wherever the plane crossed material.
// Get the sign backwards and section mode discards the half the user wanted to
// keep, so pin it down here rather than discovering it in a screenshot.
static void test_section_plane_equation() {
  s::Aabb bounds = s::Aabb::empty();
  bounds.expand({-10.0f, -10.0f, -10.0f});
  bounds.expand({ 10.0f,  10.0f,  10.0f});

  r::SectionPlane plane;
  plane.normal = {1.0f, 0.0f, 0.0f};
  plane.offset = 0.0f;

  const s::vec4 eq = plane.equation(bounds);
  // offset 0 => plane through the bounds centre (the origin here).
  CHECK(std::fabs(eq.w) < 1e-5f);
  // +normal side is kept, -normal side is cut away.
  CHECK(glm::dot(eq, s::vec4( 5.0f, 0.0f, 0.0f, 1.0f)) > 0.0f);
  CHECK(glm::dot(eq, s::vec4(-5.0f, 0.0f, 0.0f, 1.0f)) < 0.0f);

  // `offset` slides along the normal, measured from the bounds centre — not
  // from the world origin — so the anchor tracks it exactly.
  plane.offset = 4.0f;
  CHECK(std::fabs(plane.anchor(bounds).x - 4.0f) < 1e-5f);
  CHECK(glm::dot(plane.equation(bounds),
                 s::vec4(2.0f, 0.0f, 0.0f, 1.0f)) < 0.0f);

  // A non-unit normal must still yield a normalised equation, or the clip
  // distance would be scaled and the straddle radius test would be wrong.
  plane.normal = {0.0f, 3.0f, 0.0f};
  plane.offset = 0.0f;
  const s::vec4 scaled = plane.equation(bounds);
  CHECK(std::fabs(glm::length(s::vec3(scaled)) - 1.0f) < 1e-5f);

  // A degenerate normal must not produce NaNs.
  plane.normal = {0.0f, 0.0f, 0.0f};
  const s::vec4 degenerate = plane.equation(bounds);
  CHECK(std::isfinite(degenerate.x) && std::isfinite(degenerate.w));
  CHECK(std::fabs(glm::length(s::vec3(degenerate)) - 1.0f) < 1e-5f);
}

// The straddle cull is what keeps the stencil cap pass cheap, and it is only
// valid because non-straddling boxes provably cannot change the mask. If it ever
// reports false for a box the plane really cuts, that part silently loses its
// cap — a subtle wrong-looking frame rather than a crash, so it needs a test.
static void test_section_straddle_cull() {
  r::SectionPlane plane;
  plane.normal = {0.0f, 0.0f, 1.0f};
  plane.offset = 0.0f;

  s::Aabb world = s::Aabb::empty();
  world.expand({-10.0f, -10.0f, -10.0f});
  world.expand({ 10.0f,  10.0f,  10.0f});
  const s::vec4 eq = plane.equation(world);

  auto box = [](float zmin, float zmax) {
    s::Aabb b = s::Aabb::empty();
    b.expand({-1.0f, -1.0f, zmin});
    b.expand({ 1.0f,  1.0f, zmax});
    return b;
  };

  CHECK( r::box_straddles_plane(box(-1.0f,  1.0f), eq));  // cut in half
  CHECK(!r::box_straddles_plane(box( 2.0f,  5.0f), eq));  // wholly kept
  CHECK(!r::box_straddles_plane(box(-5.0f, -2.0f), eq));  // wholly cut
  CHECK(!r::box_straddles_plane(s::Aabb::empty(),  eq));  // no geometry

  // A tilted plane must use the projected extent, not a per-axis test: this box
  // clears the plane on every axis independently but its corner does cross it.
  r::SectionPlane diagonal;
  diagonal.normal = {1.0f, 1.0f, 0.0f};
  diagonal.offset = 0.0f;
  const s::vec4 diag_eq = diagonal.equation(world);
  s::Aabb corner = s::Aabb::empty();
  corner.expand({-2.0f, 1.0f, -1.0f});
  corner.expand({ 2.0f, 3.0f,  1.0f});
  CHECK(r::box_straddles_plane(corner, diag_eq));
}

// The drag clamp: at +range the plane has just cleared the far side (nothing
// cut), at -range the near side (everything cut). Anything past that is dead
// travel on the handle.
static void test_section_offset_range() {
  s::Aabb bounds = s::Aabb::empty();
  bounds.expand({-4.0f, -6.0f, -8.0f});
  bounds.expand({ 4.0f,  6.0f,  8.0f});

  r::SectionPlane plane;
  plane.normal = {0.0f, 1.0f, 0.0f};
  CHECK(std::fabs(plane.offset_range(bounds) - 6.0f) < 1e-5f);
  plane.normal = {0.0f, 0.0f, 1.0f};
  CHECK(std::fabs(plane.offset_range(bounds) - 8.0f) < 1e-5f);

  // At +range the plane has passed the far side, so the whole model is cut
  // away: even the far-most point is no longer on the kept side.
  plane.offset = plane.offset_range(bounds);
  const s::vec4 eq = plane.equation(bounds);
  CHECK(glm::dot(eq, s::vec4(0.0f, 0.0f, 8.0f, 1.0f)) <= 1e-5f);
  // ...and at -range the plane has cleared the near side, so nothing is cut.
  plane.offset = -plane.offset_range(bounds);
  const s::vec4 clear = plane.equation(bounds);
  CHECK(glm::dot(clear, s::vec4(0.0f, 0.0f, -8.0f, 1.0f)) >= -1e-5f);

  // An empty scene must not yield zero or NaN sizes — the gizmo still has to
  // draw something before anything is imported.
  const s::Aabb empty = s::Aabb::empty();
  CHECK(plane.offset_range(empty)   > 0.0f);
  CHECK(plane.seed_half_size(empty) > 0.0f);
}

// The cap and the visible plane are both the plane's cross-section through the
// scene bounds. Two properties are load-bearing: every vertex must lie INSIDE
// the bounds (otherwise the corners fall outside the camera's near/far slab and
// come back with notches bitten out), and the polygon must actually span the
// box (otherwise the cap under-covers and the cut shows through).
static void test_section_cross_section_polygon() {
  s::Aabb bounds = s::Aabb::empty();
  bounds.expand({-4.0f, -6.0f, -8.0f});
  bounds.expand({ 4.0f,  6.0f,  8.0f});

  s::vec3 poly[12];

  // Axis-aligned plane through the centre: a rectangle spanning the full face.
  r::SectionPlane plane;
  plane.normal = {0.0f, 0.0f, 1.0f};
  plane.offset = 0.0f;
  int n = r::plane_box_cross_section(plane, bounds, 0.0f, poly, 12);
  CHECK(n == 4);
  s::Aabb fit = s::Aabb::empty();
  for (int i = 0; i < n; ++i) fit.expand(poly[i]);
  CHECK(std::fabs(fit.min.x - (-4.0f)) < 1e-3f);
  CHECK(std::fabs(fit.max.y -   6.0f)  < 1e-3f);
  CHECK(std::fabs(fit.min.z) < 1e-3f && std::fabs(fit.max.z) < 1e-3f);

  // Corner-cutting diagonal plane: a hexagon, and — the point of the whole
  // routine — still entirely within the box.
  plane.normal = {1.0f, 1.0f, 1.0f};
  plane.offset = 0.0f;
  n = r::plane_box_cross_section(plane, bounds, 0.0f, poly, 12);
  CHECK(n >= 3 && n <= 12);
  for (int i = 0; i < n; ++i) {
    CHECK(poly[i].x >= bounds.min.x - 1e-3f && poly[i].x <= bounds.max.x + 1e-3f);
    CHECK(poly[i].y >= bounds.min.y - 1e-3f && poly[i].y <= bounds.max.y + 1e-3f);
    CHECK(poly[i].z >= bounds.min.z - 1e-3f && poly[i].z <= bounds.max.z + 1e-3f);
  }
  // Every vertex is on the plane, so the polygon is genuinely planar.
  const s::vec4 diag_eq = plane.equation(bounds);
  for (int i = 0; i < n; ++i) {
    CHECK(std::fabs(glm::dot(diag_eq, s::vec4(poly[i], 1.0f))) < 1e-3f);
  }

  // Pushed past the far side: nothing to cap, and the routine must say so
  // rather than hand back a degenerate sliver to rasterise.
  plane.normal = {0.0f, 0.0f, 1.0f};
  plane.offset = plane.offset_range(bounds) * 2.0f;
  CHECK(r::plane_box_cross_section(plane, bounds, 0.0f, poly, 12) == 0);

  // No scene, and too small an output buffer: both must be refused, not written.
  plane.offset = 0.0f;
  CHECK(r::plane_box_cross_section(plane, s::Aabb::empty(), 0.0f, poly, 12) == 0);
  CHECK(r::plane_box_cross_section(plane, bounds, 0.0f, poly, 3) == 0);

  // The cap's epsilon bias must move the polygon toward the KEPT side, so a
  // model face lying exactly on the section plane stays in front of the cap.
  n = r::plane_box_cross_section(plane, bounds, 0.01f, poly, 12);
  CHECK(n == 4);
  for (int i = 0; i < n; ++i) CHECK(poly[i].z > 0.0f);
}

// Axis-drag math: a screen ray aimed at a known point on the handle's axis must
// recover that point's offset along the axis, and the near-parallel case must be
// refused rather than returning a wild value.
static void test_section_axis_drag() {
  const s::vec3 origin{0.0f, 0.0f, 0.0f};
  const s::vec3 axis  {0.0f, 0.0f, 1.0f};

  // A ray from +X aimed straight at the axis point (0,0,3).
  float t = 0.0f;
  CHECK(r::closest_point_on_axis(origin, axis, {10.0f, 0.0f, 3.0f},
                                 {-1.0f, 0.0f, 0.0f}, t));
  CHECK(std::fabs(t - 3.0f) < 1e-4f);

  // Oblique ray: closest approach to the axis is still well defined.
  CHECK(r::closest_point_on_axis(origin, axis, {10.0f, 0.0f, 0.0f},
                                 glm::normalize(s::vec3(-1.0f, 0.0f, 0.5f)), t));
  CHECK(std::isfinite(t));

  // A non-unit axis must behave identically — callers pass the raw normal.
  float t_scaled = 0.0f;
  CHECK(r::closest_point_on_axis(origin, {0.0f, 0.0f, 5.0f},
                                 {10.0f, 0.0f, 3.0f},
                                 {-1.0f, 0.0f, 0.0f}, t_scaled));
  CHECK(std::fabs(t_scaled - 3.0f) < 1e-4f);

  // Axis pointing at the camera: refuse instead of dividing by ~0. The handle
  // has collapsed to a point on screen, so no pixel maps to a sane offset.
  float unused = 12345.0f;
  CHECK(!r::closest_point_on_axis(origin, axis, {0.0f, 0.0f, 10.0f},
                                  {0.0f, 0.0f, -1.0f}, unused));
  CHECK(unused == 12345.0f);   // left untouched, so the caller can hold steady
}

static s::Aabb unit_bounds(float half) {
  s::Aabb b = s::Aabb::empty();
  b.expand({-half, -half, -half});
  b.expand({ half,  half,  half});
  return b;
}

// The ring frame is the single derivation the renderer draws from and the host
// hit-tests against. If they ever disagreed the gizmo would be grabbable
// somewhere other than where it is painted, so the contract is pinned here.
static void test_section_ring_frame() {
  const s::Aabb bounds = unit_bounds(10.0f);
  r::SectionPlane plane;
  plane.normal = {0.0f, 0.0f, 1.0f};
  plane.offset = 2.0f;

  const float wpp = 0.05f;
  const float expected_radius = r::kSectionRingPx * wpp;

  struct Case { r::SectionGizmoPart part; s::vec3 axis; };
  const Case cases[3] = {
    {r::SectionGizmoPart::RotateX, {1.0f, 0.0f, 0.0f}},
    {r::SectionGizmoPart::RotateY, {0.0f, 1.0f, 0.0f}},
    {r::SectionGizmoPart::RotateZ, {0.0f, 0.0f, 1.0f}},
  };
  for (const auto& c : cases) {
    s::vec3 centre{}, e0{}, e1{};
    CHECK(r::section_ring_frame(plane, bounds, c.part, wpp, centre, e0, e1));
    // Centred on the gizmo, not on the model: the rings and the translate arrow
    // must share an origin or the manipulator reads as two loose objects.
    CHECK(glm::length(centre - plane.anchor(bounds)) < 1e-4f);
    CHECK(std::fabs(glm::length(e0) - expected_radius) < 1e-4f);
    CHECK(std::fabs(glm::length(e1) - expected_radius) < 1e-4f);
    CHECK(std::fabs(glm::dot(e0, e1)) < 1e-4f);
    // cross(e0, e1) along the ring's axis is what makes a positive drag angle
    // turn the plane the way the ring appears to turn.
    const s::vec3 n = glm::normalize(glm::cross(e0, e1));
    CHECK(glm::length(n - c.axis) < 1e-4f);
    CHECK(glm::length(r::section_ring_axis(c.part) - c.axis) < 1e-6f);
    // Parameter 0 sits on e0, quarter turn on e1 — the host samples the ring
    // with exactly this formula when hit-testing.
    CHECK(glm::length(r::section_ring_point(centre, e0, e1, 0.0f) -
                      (centre + e0)) < 1e-4f);
  }

  s::vec3 centre{}, e0{}, e1{};
  CHECK(!r::section_ring_frame(plane, s::Aabb::empty(),
                               r::SectionGizmoPart::RotateX, wpp, centre, e0,
                               e1));
  CHECK(!r::section_ring_frame(plane, bounds, r::SectionGizmoPart::RotateX,
                               0.0f, centre, e0, e1));
  // The translate arrow is not a ring and has no frame.
  CHECK(!r::section_ring_frame(plane, bounds, r::SectionGizmoPart::Translate,
                               wpp, centre, e0, e1));
  CHECK(r::section_ring_index(r::SectionGizmoPart::Translate) < 0);
  CHECK(!r::section_part_is_ring(r::SectionGizmoPart::None));
}

// A ring whose axis has drifted onto the plane normal turns the plane by
// nothing. It must not be drawn and must not be grabbable, or the user drags a
// visible control and concludes the gizmo is broken.
static void test_section_ring_gain() {
  r::SectionPlane plane;
  plane.normal = {0.0f, 0.0f, 1.0f};

  CHECK(r::section_ring_gain(plane, r::SectionGizmoPart::RotateZ) < 1e-5f);
  CHECK(std::fabs(r::section_ring_gain(plane, r::SectionGizmoPart::RotateX) -
                  1.0f) < 1e-5f);
  CHECK(std::fabs(r::section_ring_gain(plane, r::SectionGizmoPart::RotateY) -
                  1.0f) < 1e-5f);
  CHECK(!r::section_ring_live(plane, r::SectionGizmoPart::RotateZ));
  CHECK(r::section_ring_live(plane, r::SectionGizmoPart::RotateX));
  // Not a ring at all.
  CHECK(!r::section_ring_live(plane, r::SectionGizmoPart::Translate));

  // Either side of the threshold. gain is sin(angle between normal and axis).
  const float below = std::asin(r::kSectionRingMinGain * 0.5f);
  const float above = std::asin(std::min(r::kSectionRingMinGain * 2.0f, 1.0f));
  plane.normal = {std::sin(below), 0.0f, std::cos(below)};
  CHECK(!r::section_ring_live(plane, r::SectionGizmoPart::RotateZ));
  plane.normal = {std::sin(above), 0.0f, std::cos(above)};
  CHECK(r::section_ring_live(plane, r::SectionGizmoPart::RotateZ));
}

// The drag mapping: a ray aimed at a known point on the ring must recover that
// point's parameter angle, and an edge-on ring must be refused so the caller
// falls back to the screen-space mapping instead of applying a wild rotation.
static void test_section_ring_angle() {
  const s::vec3 centre{0.0f, 0.0f, 0.0f};
  const s::vec3 e0{2.0f, 0.0f, 0.0f};      // ring about Z, radius 2
  const s::vec3 e1{0.0f, 2.0f, 0.0f};
  const s::vec3 axis{0.0f, 0.0f, 1.0f};

  // Straight down the axis at the quarter-turn point.
  float angle = 0.0f;
  CHECK(r::section_ring_angle_at(centre, e0, e1, axis, {0.0f, 2.0f, 5.0f},
                                 {0.0f, 0.0f, -1.0f}, angle));
  CHECK(std::fabs(angle - 1.57079633f) < 1e-3f);

  CHECK(r::section_ring_angle_at(centre, e0, e1, axis, {2.0f, 0.0f, 5.0f},
                                 {0.0f, 0.0f, -1.0f}, angle));
  CHECK(std::fabs(angle) < 1e-3f);

  // Oblique but still facing: the intersection is well defined.
  CHECK(r::section_ring_angle_at(centre, e0, e1, axis, {0.0f, 0.0f, 5.0f},
                                 glm::normalize(s::vec3(0.4f, 0.0f, -1.0f)),
                                 angle));
  CHECK(std::isfinite(angle));

  // Edge-on: the view direction lies in the ring's plane. Refused, and the out
  // parameter is left alone so the caller can hold its current angle.
  float untouched = 4321.0f;
  CHECK(!r::section_ring_angle_at(centre, e0, e1, axis, {0.0f, 0.0f, 0.0f},
                                  {1.0f, 0.0f, 0.0f}, untouched));
  CHECK(untouched == 4321.0f);
  CHECK(!r::section_ring_angle_at(centre, e0, e1, s::vec3(0.0f),
                                  {0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, -1.0f},
                                  untouched));
  CHECK(untouched == 4321.0f);

  // Deltas are differences of two atan2 results, so they must wrap rather than
  // jump a full turn when the cursor crosses the branch cut.
  CHECK(std::fabs(r::wrap_angle(6.28318531f)) < 1e-4f);
  CHECK(std::fabs(r::wrap_angle(3.2f) - (3.2f - 6.28318531f)) < 1e-4f);
  CHECK(std::fabs(r::wrap_angle(-3.2f) - (6.28318531f - 3.2f)) < 1e-4f);
}

// Rotation pivots about the point the gizmo sits on, not about the bounds
// centre. Get that wrong and the cut swings away from where the user grabbed it.
static void test_section_rotation() {
  const s::Aabb bounds = unit_bounds(10.0f);
  const float half_pi = 1.57079633f;

  r::SectionPlane plane;
  plane.normal = {1.0f, 0.0f, 0.0f};
  plane.offset = 0.0f;

  // A quarter turn about Z carries +X onto +Y.
  r::SectionPlane turned =
    r::section_plane_rotated(plane, bounds, {0.0f, 0.0f, 1.0f}, half_pi);
  CHECK(glm::length(turned.unit_normal() - s::vec3(0.0f, 1.0f, 0.0f)) < 1e-4f);
  CHECK(std::fabs(turned.offset) < 1e-4f);

  // Off-centre: the plane must still pass through the anchor it had before the
  // rotation. That is the whole "tilt it in place" contract.
  plane.offset = 4.0f;
  const s::vec3 anchor_before = plane.anchor(bounds);
  for (float deg : {10.0f, 35.0f, 80.0f, -50.0f}) {
    const float rad = deg * 3.14159265f / 180.0f;
    turned = r::section_plane_rotated(plane, bounds, {0.0f, 1.0f, 0.0f}, rad);
    const s::vec4 eq = turned.equation(bounds);
    CHECK(std::fabs(glm::dot(s::vec3(eq), anchor_before) + eq.w) < 1e-3f);
    // ...and the normal really did turn by the angle asked for.
    CHECK(std::fabs(glm::dot(turned.unit_normal(), plane.unit_normal()) -
                    std::cos(rad)) < 1e-4f);
  }

  // offset_range depends on the normal, so a plane parked at the end of its
  // travel can tilt into a shorter one and must come back clamped.
  plane.normal = {1.0f, 0.0f, 0.0f};
  plane.offset = plane.offset_range(bounds);
  turned = r::section_plane_rotated(plane, bounds, {0.0f, 1.0f, 0.0f}, 0.7f);
  CHECK(std::fabs(turned.offset) <= turned.offset_range(bounds) + 1e-4f);

  // Degenerate inputs leave the plane exactly as it was rather than producing a
  // NaN plane equation the clip would then apply to every vertex.
  const r::SectionPlane same_axis =
    r::section_plane_rotated(plane, bounds, s::vec3(0.0f), 0.5f);
  CHECK(same_axis.offset == plane.offset);
  CHECK(glm::length(same_axis.normal - plane.normal) < 1e-6f);
  const r::SectionPlane same_angle = r::section_plane_rotated(
    plane, bounds, {0.0f, 1.0f, 0.0f},
    std::numeric_limits<float>::quiet_NaN());
  CHECK(glm::length(same_angle.normal - plane.normal) < 1e-6f);
}

// Reset is the one-click way back from a plane the rings have tilted anywhere.
static void test_section_plane_reset() {
  const s::Aabb bounds = unit_bounds(10.0f);
  r::SectionPlane plane;
  plane.normal = glm::normalize(s::vec3(-0.9f, 0.3f, 0.2f));
  plane.offset = 3.0f;
  CHECK(!r::section_plane_is_reset(plane, bounds));

  const r::SectionPlane reset = r::section_plane_reset(plane);
  // Nearest world axis, sign kept: a reset must never flip which half of the
  // model survives.
  CHECK(glm::length(reset.unit_normal() - s::vec3(-1.0f, 0.0f, 0.0f)) < 1e-5f);
  CHECK(reset.offset == 0.0f);
  CHECK(r::section_plane_is_reset(reset, bounds));

  // An axis-aligned plane that has merely been slid is not reset yet.
  r::SectionPlane slid;
  slid.normal = {0.0f, 1.0f, 0.0f};
  slid.offset = 2.0f;
  CHECK(!r::section_plane_is_reset(slid, bounds));
  CHECK(r::section_plane_is_reset(r::section_plane_reset(slid), bounds));
}

static void test_importer_registry() {
  auto& reg = c::ImporterRegistry::instance();
  CHECK(reg.select("foo.step") != nullptr);
  CHECK(reg.select("foo.STP")  != nullptr);
  CHECK(reg.select("foo.iges") != nullptr);
  CHECK(reg.select("foo.bin")  == nullptr);
}

static void test_tessellation_policy() {
  c::ImportOptions opts;
  opts.tessellation_mode = c::TessellationMode::VisualRelative;
  opts.target_screen_error_px = 0.5;
  opts.reference_screen_pixels = 2000.0;
  opts.min_linear_deflection = 0.01;
  opts.max_relative_deflection = 1.0 / 2000.0;

  s::Aabb large = s::Aabb::empty();
  large.expand({0.0f, 0.0f, 0.0f});
  large.expand({38431.0f, 10.0f, 10.0f});
  auto resolved = c::resolve_tessellation_policy(opts, large);
  CHECK(resolved.options.tessellation_mode == c::TessellationMode::VisualRelative);
  CHECK(!resolved.options.relative_deflection);
  CHECK(std::fabs(resolved.options.linear_deflection - 9.60775) < 1e-4);

  s::Aabb small = s::Aabb::empty();
  small.expand({0.0f, 0.0f, 0.0f});
  small.expand({0.1f, 0.1f, 0.1f});
  resolved = c::resolve_tessellation_policy(opts, small);
  CHECK(std::fabs(resolved.options.linear_deflection - 0.01) < 1e-9);

  opts.target_screen_error_px = 10.0;
  opts.reference_screen_pixels = 1000.0;
  resolved = c::resolve_tessellation_policy(opts, large);
  CHECK(std::fabs(resolved.options.linear_deflection - 19.2155) < 1e-4);

  opts.tessellation_mode = c::TessellationMode::Absolute;
  opts.linear_deflection = 0.123;
  opts.relative_deflection = true;
  resolved = c::resolve_tessellation_policy(opts, large);
  CHECK(resolved.options.tessellation_mode == c::TessellationMode::Absolute);
  CHECK(resolved.options.relative_deflection);
  CHECK(std::fabs(resolved.options.linear_deflection - 0.123) < 1e-9);
}

// A box poisoned by unbounded geometry (±inf once cast to float — OCCT
// reports "open" boxes for e.g. untrimmed conical faces) must not leak an
// infinite deflection; the policy has to fall back to absolute mode.
static void test_tessellation_policy_unbounded() {
  c::ImportOptions opts;
  opts.tessellation_mode = c::TessellationMode::VisualRelative;
  opts.linear_deflection = 0.1;

  s::Aabb bad = s::Aabb::empty();
  bad.expand({0.0f, 0.0f, 0.0f});
  bad.expand({std::numeric_limits<float>::infinity(), 10.0f, 10.0f});
  const auto resolved = c::resolve_tessellation_policy(opts, bad);
  CHECK(resolved.model_extent == 0.0);
  CHECK(resolved.options.tessellation_mode == c::TessellationMode::Absolute);
  CHECK(std::isfinite(resolved.options.linear_deflection));
  CHECK(std::fabs(resolved.options.linear_deflection - 0.1) < 1e-9);
}

static void test_hammer_iges_visual_relative_import() {
#ifdef CADLY_TEST_SOURCE_ROOT
  const std::filesystem::path path =
    std::filesystem::path(CADLY_TEST_SOURCE_ROOT) / "test_files" / "hammer.iges";
  if (!std::filesystem::exists(path)) return;

  c::ImportOptions opts;
  auto result = c::ImporterRegistry::instance().import(path, opts);
  CHECK(result.success);
  CHECK(result.scene != nullptr);
  CHECK(result.summary.tessellation_mode == c::TessellationMode::VisualRelative);
  CHECK(result.summary.model_extent > 10000.0);
  CHECK(result.summary.resolved_linear_deflection > 1.0);
  CHECK(result.summary.triangle_count > 0);
  CHECK(result.summary.triangle_count < 100000);
#endif
}

// Periodic CAD faces duplicate their seam vertices in the triangulation. A
// triangle-only normal average gives those copies different normals, so a
// silhouette aligned with the seam has no zero crossing and loses one
// generator line. The local screw fixture is ignored by git, so this check is
// conditional and remains useful on developer machines that have it.
static void test_periodic_face_normals() {
#ifdef CADLY_TEST_SOURCE_ROOT
  const std::filesystem::path screw =
    std::filesystem::path(CADLY_TEST_SOURCE_ROOT) / "test_files" / "screw.step";
  if (!std::filesystem::exists(screw)) return;

  c::ImportOptions opts;
  opts.tessellation_mode = c::TessellationMode::Absolute;
  opts.linear_deflection = 0.1;
  const auto result = c::ImporterRegistry::instance().import(screw, opts);
  CHECK(result.success);
  if (!result.scene) return;

  bool found_cylinder = false;
  for (const auto& mesh : result.scene->meshes) {
    if (!mesh) continue;
    for (const auto& sub : mesh->submeshes) {
      const s::vec3 extent = sub.bounds.max - sub.bounds.min;
      if (extent.x < 9.0f || extent.x > 11.0f ||
          extent.y < 9.0f || extent.y > 11.0f || extent.z < 30.0f) {
        continue;
      }

      const float seam_x = sub.bounds.max.x;
      const float seam_y = 0.5f * (sub.bounds.min.y + sub.bounds.max.y);
      std::size_t seam_vertices = 0;
      float worst_seam_normal_y = 0.0f;
      const auto end = static_cast<std::size_t>(sub.index_offset) +
                       static_cast<std::size_t>(sub.index_count);
      for (std::size_t i = sub.index_offset; i < end; ++i) {
        const auto& v = mesh->vertices[mesh->indices[i]];
        if (std::fabs(v.position.x - seam_x) < 1e-3f &&
            std::fabs(v.position.y - seam_y) < 1e-3f) {
          ++seam_vertices;
          worst_seam_normal_y = std::max(worst_seam_normal_y,
                                         std::fabs(v.normal.y));
        }
      }
      CHECK(seam_vertices >= 2);
      CHECK(worst_seam_normal_y < 1e-3f);
      found_cylinder = true;
    }
  }
  CHECK(found_cylinder);
#endif
}

// Importers must close their XCAF document on every return path; a document
// left open keeps the whole OCAF graph alive in the process-wide application
// session, so repeated imports ratchet memory upward. Exercise both the
// success path and the ReadFile-failure path and assert the session is empty
// afterwards.
static void test_xcaf_documents_closed_after_import() {
  CHECK(c::open_xcaf_document_count() == 0);

#ifdef CADLY_TEST_SOURCE_ROOT
  const std::filesystem::path hammer =
    std::filesystem::path(CADLY_TEST_SOURCE_ROOT) / "test_files" / "hammer.iges";
  if (std::filesystem::exists(hammer)) {
    c::ImportOptions opts;
    for (int i = 0; i < 3; ++i) {
      auto result = c::ImporterRegistry::instance().import(hammer, opts);
      CHECK(result.success);
      CHECK(c::open_xcaf_document_count() == 0);
    }
  }
#endif

  // Failure path: a file with a STEP extension but garbage contents makes
  // ReadFile fail after the document has been created.
  const auto garbage = std::filesystem::temp_directory_path() /
    "cadly_smoke_garbage.step";
  {
    std::ofstream out(garbage);
    out << "this is not a STEP file\n";
  }
  c::ImportOptions opts;
  auto result = c::ImporterRegistry::instance().import(garbage, opts);
  CHECK(!result.success);
  CHECK(c::open_xcaf_document_count() == 0);
  std::filesystem::remove(garbage);
}

// Importers must surface cancellation explicitly: a cancelled import may
// carry a partial scene, and callers must never mistake it for a completed
// load (success) or a parse error (plain failure). The always-cancelled sink
// trips the earliest poll; deeper OCCT-stage cancellation (Transfer, batch
// meshing) rides the same IProgressSink::cancelled() via OcctProgressBridge.
static void test_import_cancellation_flagged() {
#ifdef CADLY_TEST_SOURCE_ROOT
  class CancelledSink final : public c::IProgressSink {
  public:
    void update(float, const std::string&) override {}
    bool cancelled() const override { return true; }
  };

  const std::filesystem::path hammer =
    std::filesystem::path(CADLY_TEST_SOURCE_ROOT) / "test_files" / "hammer.iges";
  if (!std::filesystem::exists(hammer)) return;

  CancelledSink sink;
  auto result = c::ImporterRegistry::instance().import(hammer, {}, &sink);
  CHECK(result.cancelled);
  CHECK(!result.success);
  CHECK(c::open_xcaf_document_count() == 0);
#endif
}

// Colour must have exactly one source: the per-submesh materials. The old
// pipeline baked the shape colour into the vertices AND assigned it to the
// submesh material; the shader multiplies both, so every imported colour was
// squared (darkened), and face colours were cross-tinted by the shape
// colour. Vertices must now stay neutral white, and each submesh must point
// at a valid material with source_face_id preserving explorer order (the
// contract the face->material pairing relies on when faces are skipped).
static void test_step_colors_single_source() {
#ifdef CADLY_TEST_SOURCE_ROOT
  // KR600 is the smallest sample whose colours OCCT's XCAF reader actually
  // maps (as1-ug-214 styles predate what it translates). Coarse absolute
  // deflection keeps the meshing share of the test cheap; colour handling
  // is independent of tessellation density.
  const std::filesystem::path colored =
    std::filesystem::path(CADLY_TEST_SOURCE_ROOT) / "test_files" /
    "KR600_R2830-4.stp";
  if (!std::filesystem::exists(colored)) return;

  c::ImportOptions opts;
  opts.tessellation_mode  = c::TessellationMode::Absolute;
  opts.linear_deflection  = 2.0;
  auto result = c::ImporterRegistry::instance().import(colored, opts);
  CHECK(result.success);
  if (!result.scene) return;
  const auto& scn = *result.scene;

  // The sample carries part/face colours; they must arrive as materials.
  CHECK(scn.materials.size() > 1);

  std::size_t tinted_vertices  = 0;
  std::size_t bad_material_ref = 0;
  std::size_t unordered_faces  = 0;
  for (const auto& mesh : scn.meshes) {
    if (!mesh) continue;
    for (const auto& v : mesh->vertices) {
      if (v.color_rgba8 != 0xFFFFFFFFu) ++tinted_vertices;
    }
    bool first = true;
    std::uint32_t prev_face = 0;
    for (const auto& sub : mesh->submeshes) {
      if (sub.material_index >= scn.materials.size()) ++bad_material_ref;
      if (!first && sub.source_face_id <= prev_face) ++unordered_faces;
      prev_face = sub.source_face_id;
      first = false;
    }
  }
  CHECK(tinted_vertices == 0);
  CHECK(bad_material_ref == 0);
  CHECK(unordered_faces == 0);
#endif
}

int main() {
  test_aabb();
  test_transform_roundtrip();
  test_scene_hierarchy();
  test_camera_fit();
  test_camera_orbit_screen_space();
  test_display_mode_defaults();
  test_section_plane_equation();
  test_section_straddle_cull();
  test_section_offset_range();
  test_section_cross_section_polygon();
  test_section_axis_drag();
  test_section_ring_frame();
  test_section_ring_gain();
  test_section_ring_angle();
  test_section_rotation();
  test_section_plane_reset();
  test_importer_registry();
  test_tessellation_policy();
  test_tessellation_policy_unbounded();
  test_hammer_iges_visual_relative_import();
  test_periodic_face_normals();
  test_xcaf_documents_closed_after_import();
  test_import_cancellation_flagged();
  test_step_colors_single_source();
  if (g_failures == 0) {
    std::printf("OK: scene + cad smoke tests passed.\n");
    return 0;
  }
  std::fprintf(stderr, "%d failures.\n", g_failures);
  return 1;
}

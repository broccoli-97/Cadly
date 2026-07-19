// Tiny smoke test — verifies that the renderer-free scene math and importer
// registry are wired together. Real importer/triangulation tests need sample
// STEP files; those land in the test corpus once we collect a public set.

#include "cadly/cad/ImporterRegistry.h"
#include "cadly/cad/TessellationPolicy.h"
#include "cadly/cad/XcafSession.h"
#include "cadly/renderer/RenderTypes.h"
#include "cadly/scene/Aabb.h"
#include "cadly/scene/Camera.h"
#include "cadly/scene/Scene.h"
#include "cadly/scene/Transform.h"

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
  test_importer_registry();
  test_tessellation_policy();
  test_tessellation_policy_unbounded();
  test_hammer_iges_visual_relative_import();
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

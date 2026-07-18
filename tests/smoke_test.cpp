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

int main() {
  test_aabb();
  test_transform_roundtrip();
  test_scene_hierarchy();
  test_camera_fit();
  test_display_mode_defaults();
  test_importer_registry();
  test_tessellation_policy();
  test_tessellation_policy_unbounded();
  test_hammer_iges_visual_relative_import();
  test_xcaf_documents_closed_after_import();
  if (g_failures == 0) {
    std::printf("OK: scene + cad smoke tests passed.\n");
    return 0;
  }
  std::fprintf(stderr, "%d failures.\n", g_failures);
  return 1;
}

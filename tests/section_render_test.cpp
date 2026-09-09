#include "cadly/renderer_gl/GLRenderer.h"
#include "cadly/scene/Scene.h"

#include <QGuiApplication>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>

namespace s = cadly::scene;
namespace r = cadly::renderer;

namespace {

constexpr int kSize = 512;
int failures = 0;

void check(bool ok, const char* message) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
  }
}

int color_distance(QRgb a, QRgb b) {
  return std::max({std::abs(qRed(a) - qRed(b)),
                   std::abs(qGreen(a) - qGreen(b)),
                   std::abs(qBlue(a) - qBlue(b)),
                   std::abs(qAlpha(a) - qAlpha(b))});
}

void check_patch_equal(const QImage& a, const QImage& b, QPoint at,
                       const char* message) {
  int delta = 0;
  for (int y = at.y() - 8; y <= at.y() + 8; ++y) {
    for (int x = at.x() - 8; x <= at.x() + 8; ++x) {
      delta = std::max(delta, color_distance(a.pixel(x, y), b.pixel(x, y)));
    }
  }
  check(delta <= 1, message);
}

QPoint project(const s::Camera& camera, const s::vec3& world) {
  const s::vec4 clip = camera.view_proj() * s::vec4(world, 1.0f);
  const s::vec3 ndc = s::vec3(clip) / clip.w;
  return {static_cast<int>((ndc.x * 0.5f + 0.5f) * kSize),
          static_cast<int>((0.5f - ndc.y * 0.5f) * kSize)};
}

void add_box(s::Scene& scene, const s::vec3& lo, const s::vec3& hi,
             std::uint32_t material) {
  auto mesh = std::make_shared<s::Mesh>();
  const s::vec3 centre = (lo + hi) * 0.5f;
  const s::vec3 half = (hi - lo) * 0.5f;
  for (int axis = 0; axis < 3; ++axis) {
    for (float sign : {-1.0f, 1.0f}) {
      s::vec3 normal(0.0f), u(0.0f), v(0.0f);
      normal[axis] = sign;
      u[(axis + 1) % 3] = half[(axis + 1) % 3];
      v[(axis + 2) % 3] = half[(axis + 2) % 3] * sign;
      const s::vec3 face = centre + normal * half[axis];
      const auto first = static_cast<std::uint32_t>(mesh->vertices.size());
      for (const s::vec3& p : {face - u - v, face + u - v,
                               face + u + v, face - u + v}) {
        mesh->vertices.push_back({p, normal});
      }
      for (std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) {
        mesh->indices.push_back(first + i);
      }
    }
  }
  mesh->bounds.expand(lo);
  mesh->bounds.expand(hi);
  s::Submesh sub;
  sub.index_count = static_cast<std::uint32_t>(mesh->indices.size());
  sub.material_index = material;
  sub.bounds = mesh->bounds;
  mesh->submeshes.push_back(sub);
  s::Node node;
  node.mesh_index = scene.add_mesh(mesh);
  scene.add_node(std::move(node));
}

void place_section(r::DisplayMode& mode, const s::Scene& scene, float z) {
  mode.section.normal = {0.0f, 0.0f, -1.0f};
  mode.section.offset = glm::dot(mode.section.normal,
                                 s::vec3(0.0f, 0.0f, z) -
                                   scene.world_bounds.center());
}

void verify_section(r::IRenderer& renderer, QOpenGLFramebufferObject& target,
                     s::Scene& scene, int samples, s::Projection projection) {
  std::fprintf(stdout, "Section pixels: MSAA %d, %s\n", samples,
               projection == s::Projection::Orthographic ? "ortho" : "perspective");
  scene.camera.projection_mode = projection;
  scene.camera.orientation = s::quat(1.0f, 0.0f, 0.0f, 0.0f);
  scene.nodes[2].visible = true;
  scene.nodes[2].ghosted = false;

  r::DisplayMode mode;
  mode.msaa_samples = samples;
  mode.show_axes = false;
  mode.show_scale_bar = false;
  mode.show_edges = false;
  mode.show_hidden_edges = false;
  mode.section_enabled = true;
  mode.section_show_plane = false;
  mode.section_hatch = false;
  mode.background_top = mode.background_bottom = s::vec3(0.08f);
  place_section(mode, scene, 0.0f);

  auto draw = [&]() {
    target.bind();
    renderer.render(mode);
    return target.toImage();
  };
  auto backdrop = [&](const s::vec3& color) {
    scene.materials[1].base_color = s::vec4(color, 1.0f);
    scene.materials[1].emissive_color = color;
    scene.materials[1].emissive = 1.0f;
  };

  const QPoint cap = project(scene.camera, {-1.5f, 1.0f, 0.0f});
  // Outside the manipulator's 78-pixel radius and clear of the plane border.
  const QPoint gap = project(scene.camera, {0.0f, 1.3f, 0.0f});
  backdrop({0.95f, 0.02f, 0.03f});
  const QImage solid = draw();
  const s::vec3 fill = mode.section_cap_color * 255.0f;
  check(color_distance(solid.pixel(cap),
                        qRgb(static_cast<int>(std::lround(fill.r)),
                             static_cast<int>(std::lround(fill.g)),
                             static_cast<int>(std::lround(fill.b)))) <= 1,
        "unhatched cap must replace the surface behind it with opaque fill");

  mode.section_hatch = true;
  const QImage red_behind = draw();
  backdrop({0.02f, 0.12f, 0.95f});
  mode.background_top = mode.background_bottom = s::vec3(0.65f);
  const QImage blue_behind = draw();
  check_patch_equal(red_behind, blue_behind, cap,
                     "hatch and its gaps must not transmit geometry/background");
  check(color_distance(red_behind.pixel(gap), blue_behind.pixel(gap)) > 40,
        "the gap between cut solids must remain open");

  int darkest = 255, lightest = 0;
  bool opaque = true;
  for (int y = cap.y() - 8; y <= cap.y() + 8; ++y) {
    for (int x = cap.x() - 8; x <= cap.x() + 8; ++x) {
      const QRgb pixel = blue_behind.pixel(x, y);
      darkest = std::min(darkest, qGreen(pixel));
      lightest = std::max(lightest, qGreen(pixel));
      opaque = opaque && qAlpha(pixel) == 255;
    }
  }
  check(opaque, "hatch must preserve framebuffer opacity");
  check(lightest - darkest > 20, "the cut must contain visible hatch strokes");

  mode.section_show_plane = true;
  const QImage preview = draw();
  check_patch_equal(blue_behind, preview, cap,
                     "showing the auxiliary plane must not tint the cap");
  const int tint = color_distance(blue_behind.pixel(gap), preview.pixel(gap));
  check(tint > 0 && tint < 40,
        "uncut auxiliary plane must lightly tint, not cover, geometry behind it");
  check(qAlpha(preview.pixel(gap)) == 255,
        "translucent preview must not make the window itself transparent");
  backdrop({0.95f, 0.02f, 0.03f});
  const QImage red_preview = draw();
  check_patch_equal(preview, red_preview, cap,
                     "cap must remain opaque with the auxiliary plane shown");
  check(color_distance(preview.pixel(gap), red_preview.pixel(gap)) > 40,
        "geometry must remain visible through uncut auxiliary plane");

  // Ghost surfaces draw after the cap, exercising its depth writes as well as
  // its colour replacement. The rear box is wholly on the retained side.
  scene.nodes[2].ghosted = true;
  mode.ghost_opacity = 0.8f;
  const QImage ghost = draw();
  check_patch_equal(red_preview, ghost, cap,
                     "cap depth must occlude later translucent geometry");
  scene.nodes[2].ghosted = false;

  // A plane between disconnected bodies has no cut material anywhere. The
  // entire preview must be transparent, including the previous cap footprint.
  place_section(mode, scene, -0.9f);
  const QImage between = draw();
  mode.section_show_plane = false;
  const QImage between_no_plane = draw();
  const QPoint former_cap = project(scene.camera, {-1.5f, 1.0f, -0.9f});
  const int between_tint = color_distance(between.pixel(former_cap),
                                          between_no_plane.pixel(former_cap));
  check(between_tint > 0 && between_tint < 40,
        "a plane cutting no solid must have no opaque fill or stale mask");

  place_section(mode, scene, 0.0f);
  mode.hidden_line = true;
  const QImage paper = draw();
  mode.section_show_plane = true;
  const QImage paper_preview = draw();
  check_patch_equal(paper, paper_preview, cap,
                     "auxiliary plane must not stain hidden-line hatch");

  mode.hidden_line = false;
  mode.wireframe = true;
  const QImage wire = draw();
  check(color_distance(wire.pixel(cap), wire.pixel(gap)) <= 1,
        "wireframe must not inherit a cap or its preview exclusion mask");

  mode.wireframe = false;
  mode.section_show_plane = false;
  scene.nodes[2].visible = false;
  scene.camera.orientation = glm::angleAxis(glm::pi<float>(),
                                             s::vec3(0.0f, 1.0f, 0.0f));
  const QPoint back = project(scene.camera, {-1.5f, 1.0f, -0.5f});
  const QImage back_cut = draw();
  mode.section_enabled = false;
  const QImage back_whole = draw();
  check_patch_equal(back_cut, back_whole, back,
                     "retained outer surfaces must occlude the cap from behind");
}

} // namespace

int main(int argc, char** argv) {
  qputenv("CADLY_ASSET_ROOT", CADLY_TEST_SOURCE_ROOT);
  QGuiApplication app(argc, argv);
  QSurfaceFormat format;
  format.setVersion(4, 1);
  format.setProfile(QSurfaceFormat::CoreProfile);
  format.setDepthBufferSize(24);
  format.setStencilBufferSize(8);
  QOpenGLContext context;
  context.setFormat(format);
  if (!context.create()) {
    std::fprintf(stdout, "SKIP: no desktop OpenGL context available\n");
    return 77;
  }
  QOffscreenSurface surface;
  surface.setFormat(context.format());
  surface.create();
  if (!context.makeCurrent(&surface) || context.isOpenGLES() ||
      context.format().version() < qMakePair(4, 1)) {
    std::fprintf(stdout, "SKIP: OpenGL 4.1 core offscreen rendering unavailable\n");
    return 77;
  }

  QOpenGLFramebufferObjectFormat target_format;
  target_format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
  target_format.setInternalTextureFormat(GL_RGBA8);
  QOpenGLFramebufferObject target(kSize, kSize, target_format);
  if (!target.isValid() || !target.bind()) return 1;
  auto renderer = cadly::renderer_gl::make_gl_renderer([&](const char* name) {
    return reinterpret_cast<void*>(context.getProcAddress(name));
  });
  renderer->initialize();
  renderer->resize(kSize, kSize);

  auto scene = std::make_shared<s::Scene>();
  scene->materials.resize(2);
  add_box(*scene, {-2.5f, -1.6f, -0.5f}, {-0.6f, 1.6f, 0.5f}, 0);
  add_box(*scene, {0.6f, -1.6f, -0.5f}, {2.5f, 1.6f, 0.5f}, 0);
  add_box(*scene, {-2.5f, -1.6f, -1.6f}, {2.5f, 1.6f, -1.4f}, 1);
  scene->update_transforms();
  scene->camera.target = s::vec3(0.0f);
  scene->camera.distance = 8.0f;
  renderer->attach_scene(scene);
  r::DisplayMode warmup;
  warmup.msaa_samples = 0;
  for (int frame = 0; frame < 1024 && renderer->needs_redraw(); ++frame) {
    target.bind();
    renderer->render(warmup);
  }
  check(!renderer->needs_redraw(), "lighting must settle before image comparisons");

  for (int samples : {0, 4}) {
    for (s::Projection projection : {s::Projection::Orthographic,
                                     s::Projection::Perspective}) {
      verify_section(*renderer, target, *scene, samples, projection);
    }
  }
  renderer->shutdown();
  check(context.functions()->glGetError() == GL_NO_ERROR,
        "section rendering must not produce OpenGL errors");
  return failures == 0 ? 0 : 1;
}

#include "cadly/renderer_gl/GLRenderer.h"
#include "cadly/scene/Scene.h"

#include <QDir>
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

int patch_distance(const QImage& a, const QImage& b, QPoint at) {
  int delta = 0;
  for (int y = at.y() - 8; y <= at.y() + 8; ++y) {
    for (int x = at.x() - 8; x <= at.x() + 8; ++x) {
      delta = std::max(delta, color_distance(a.pixel(x, y), b.pixel(x, y)));
    }
  }
  return delta;
}

void check_patch_equal(const QImage& a, const QImage& b, QPoint at,
                       const char* message) {
  check(patch_distance(a, b, at) <= 1, message);
}

QPoint project(const s::Camera& camera, const s::vec3& world) {
  const s::vec4 clip = camera.view_proj() * s::vec4(world, 1.0f);
  const s::vec3 ndc = s::vec3(clip) / clip.w;
  return {static_cast<int>((ndc.x * 0.5f + 0.5f) * kSize),
          static_cast<int>((0.5f - ndc.y * 0.5f) * kSize)};
}

void append_box(s::Mesh& mesh, const s::vec3& lo, const s::vec3& hi,
                std::uint32_t material, bool inward = false) {
  s::Submesh sub;
  sub.index_offset = static_cast<std::uint32_t>(mesh.indices.size());
  const s::vec3 centre = (lo + hi) * 0.5f;
  const s::vec3 half = (hi - lo) * 0.5f;
  for (int axis = 0; axis < 3; ++axis) {
    for (float sign : {-1.0f, 1.0f}) {
      s::vec3 normal(0.0f), u(0.0f), v(0.0f);
      normal[axis] = sign;
      u[(axis + 1) % 3] = half[(axis + 1) % 3];
      v[(axis + 2) % 3] = half[(axis + 2) % 3] * sign;
      const s::vec3 face = centre + normal * half[axis];
      const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
      for (const s::vec3& p : {face - u - v, face + u - v,
                               face + u + v, face - u + v}) {
        mesh.vertices.push_back({p, inward ? -normal : normal});
      }
      if (inward) {
        for (std::uint32_t i : {0u, 2u, 1u, 0u, 3u, 2u}) {
          mesh.indices.push_back(first + i);
        }
      } else {
        for (std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) {
          mesh.indices.push_back(first + i);
        }
      }
    }
  }
  mesh.bounds.expand(lo);
  mesh.bounds.expand(hi);
  sub.index_count = static_cast<std::uint32_t>(mesh.indices.size()) - sub.index_offset;
  sub.material_index = material;
  sub.bounds.expand(lo);
  sub.bounds.expand(hi);
  mesh.submeshes.push_back(sub);
}

void add_box(s::Scene& scene, const s::vec3& lo, const s::vec3& hi,
             std::uint32_t material) {
  auto mesh = std::make_shared<s::Mesh>();
  append_box(*mesh, lo, hi, material);
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
  mode.section_translucent = false;
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

void check_cap_blend(const QImage& image, const QImage& behind, QPoint cap,
                     const s::vec3& fill, const char* message) {
  // A render with cap generation disabled is an independent compositing input.
  const QRgb background = behind.pixel(cap);
  const auto channel = [](float color, int rear) {
    return static_cast<int>(std::lround(color * 255.0f * 0.35f +
                                        static_cast<float>(rear) * 0.65f));
  };
  const QRgb expected = qRgb(channel(fill.r, qRed(background)),
                             channel(fill.g, qGreen(background)),
                             channel(fill.b, qBlue(background)));
  check(color_distance(image.pixel(cap), expected) <= 2, message);
}

void verify_coplanar_cap(r::IRenderer& renderer, QOpenGLFramebufferObject& target,
                         s::Scene& scene, int samples, s::Projection projection) {
  scene.camera.projection_mode = projection;
  r::DisplayMode mode;
  mode.msaa_samples = samples;
  mode.show_axes = mode.show_scale_bar = false;
  mode.section_enabled = true;
  mode.section_show_plane = false;
  place_section(mode, scene, 0.0f);
  auto draw = [&]() {
    target.bind();
    renderer.render(mode);
    return target.toImage();
  };
  for (const bool show_edges : {false, true}) {
    mode.show_edges = show_edges;
    for (const bool translucent : {false, true}) {
      mode.section_translucent = translucent;
      for (const float degrees : {0.0f, 0.1f, 1.0f, 5.0f, 15.0f, 35.0f, 60.0f, 75.0f}) {
        scene.camera.orientation =
          glm::angleAxis(glm::radians(degrees), s::vec3(0, 1, 0)) *
          glm::angleAxis(glm::radians(degrees * 0.37f), s::vec3(1, 0, 0));
        // The retained plate has an existing face on the section plane. A second
        // intersected body contributes stencil there; its cap must stay behind
        // that face for every camera angle, just as when no cap is generated.
        scene.meshes[1]->double_sided = true;
        const QImage reference = draw();
        scene.meshes[1]->double_sided = false;
        const QImage capped = draw();
        const int delta = patch_distance(reference, capped,
                                          project(scene.camera, s::vec3(0.0f)));
        if (delta > 1) {
          std::fprintf(stderr, "Coplanar cap: MSAA %d, projection %d, edges %d, "
                               "translucent %d, angle %.1f, delta %d\n",
                        samples, static_cast<int>(projection), show_edges,
                        translucent, degrees, delta);
        }
        check(delta <= 1, "orbiting must not let a cap bleed over a coplanar retained face");

        const QString capture_dir = qEnvironmentVariable("CADLY_SECTION_TEST_CAPTURE_DIR");
        if (!capture_dir.isEmpty() && samples == 4 &&
            projection == s::Projection::Perspective && show_edges &&
            translucent && degrees == 15.0f) {
          check(QDir().mkpath(capture_dir), "create coplanar cap capture directory");
          check(reference.save(QDir(capture_dir).filePath("coplanar-reference.png")),
                "save coplanar retained face");
          check(capped.save(QDir(capture_dir).filePath("coplanar-cap.png")),
                "save coplanar cap occlusion");
        }
      }
    }
  }
}

void verify_touching_solids(r::IRenderer& renderer, QOpenGLFramebufferObject& target,
                            s::Scene& scene, int samples, s::Projection projection) {
  scene.camera.projection_mode = projection;
  r::DisplayMode mode;
  mode.msaa_samples = samples;
  mode.show_axes = mode.show_scale_bar = false;
  mode.section_enabled = true;
  mode.section_show_plane = false;
  place_section(mode, scene, 0.0f);
  auto draw = [&]() {
    target.bind();
    renderer.render(mode);
    return target.toImage();
  };
  for (const bool show_edges : {false, true}) {
    mode.show_edges = show_edges;
    for (const float scale : {1.0f, -1.0f}) {
      for (auto& node : scene.nodes) node.local.scale.x = scale;
      scene.update_transforms();
      for (int order = 0; order < 2; ++order) {
        for (const float degrees : {0.0f, 0.1f, 1.0f, 15.0f, 35.0f, 60.0f}) {
          scene.camera.orientation =
            glm::angleAxis(glm::radians(degrees), s::vec3(0, 1, 0)) *
            glm::angleAxis(glm::radians(degrees * 0.37f), s::vec3(1, 0, 0));
          // The opened plate's back wall and the rear body's front face touch.
          // The true front material must win consistently; a back-face tint
          // must not bleed through it as the camera or draw order changes.
          mode.backface_color = {0.85f, 0.22f, 0.18f};
          const QImage red = draw();
          mode.backface_color = {0.18f, 0.32f, 0.85f};
          const QImage blue = draw();
          const int delta = patch_distance(red, blue,
                                            project(scene.camera, scene.world_bounds.center()));
          if (delta > 1) {
            std::fprintf(stderr, "Touching solids: MSAA %d, projection %d, edges %d, "
                                 "scale %.0f, order %d, angle %.1f, delta %d\n",
                          samples, static_cast<int>(projection), show_edges,
                          scale, order, degrees, delta);
          }
          check(delta <= 1, "coplanar front faces must occlude section back walls while orbiting");
        }
        std::swap(scene.nodes[0], scene.nodes[1]);
      }
    }
  }
}

void verify_translucent(r::IRenderer& renderer, QOpenGLFramebufferObject& target,
                        s::Scene& scene, int samples, s::Projection projection) {
  scene.camera.projection_mode = projection;
  scene.camera.orientation = s::quat(1.0f, 0.0f, 0.0f, 0.0f);
  scene.nodes[2].visible = true;
  scene.nodes[2].ghosted = false;
  r::DisplayMode mode;
  mode.msaa_samples = samples;
  mode.show_axes = mode.show_scale_bar = mode.show_edges = false;
  mode.section_enabled = true;
  mode.section_show_plane = false;
  mode.section_hatch = false;
  mode.background_top = mode.background_bottom = s::vec3(0.08f);
  place_section(mode, scene, 0.0f);
  const QPoint cap = project(scene.camera, {-1.5f, 1.0f, 0.0f});
  const QPoint gap = project(scene.camera, {0.0f, 1.3f, 0.0f});
  auto draw = [&]() {
    target.bind();
    renderer.render(mode);
    return target.toImage();
  };
  auto uncapped = [&]() {
    scene.meshes[0]->double_sided = scene.meshes[1]->double_sided = true;
    const QImage image = draw();
    scene.meshes[0]->double_sided = scene.meshes[1]->double_sided = false;
    return image;
  };
  auto backdrop = [&](const s::vec3& color) {
    scene.materials[1].base_color = s::vec4(color, 1.0f);
    scene.materials[1].emissive_color = color;
    scene.materials[1].emissive = 1.0f;
  };
  backdrop({0.95f, 0.02f, 0.03f});
  const QImage back_wall = uncapped();
  const QImage red_behind = draw();
  check_cap_blend(red_behind, back_wall, cap, mode.section_cap_color,
                  "default cap must blend over the retained back wall at 35% opacity");
  backdrop({0.02f, 0.12f, 0.95f});
  const QImage blue_behind = draw();
  check_patch_equal(red_behind, blue_behind, cap,
                     "retained back wall must occlude unrelated geometry behind the cut solid");
  check(color_distance(red_behind.pixel(gap), blue_behind.pixel(gap)) > 40,
        "geometry behind an actual opening must stay visible");

  mode.section_hatch = true;
  mode.backface_color = {0.85f, 0.22f, 0.18f};
  const QImage red = draw();
  mode.backface_color = {0.18f, 0.32f, 0.85f};
  const QImage blue = draw();
  int least_change = 255, darkest = 255, lightest = 0;
  bool opaque_framebuffer = true;
  for (int y = cap.y() - 8; y <= cap.y() + 8; ++y) {
    for (int x = cap.x() - 8; x <= cap.x() + 8; ++x) {
      const QRgb pixel = blue.pixel(x, y);
      least_change = std::min(least_change, color_distance(red.pixel(x, y), pixel));
      darkest = std::min(darkest, qGreen(pixel));
      lightest = std::max(lightest, qGreen(pixel));
      opaque_framebuffer = opaque_framebuffer && qAlpha(pixel) == 255;
    }
  }
  check(least_change > 40, "interior color must show through both hatch strokes and gaps");
  check(lightest - darkest > 15, "translucent hatch must remain legible");
  check(opaque_framebuffer, "cap transparency must preserve window opacity");
  mode.section_show_plane = true;
  const QImage preview = draw();
  check_patch_equal(blue, preview, cap, "plane preview must not tint a translucent cap");

  mode.section_show_plane = false;
  mode.section_hatch = false;
  // Put the ghost inside the cut solid, ahead of its newly visible back wall.
  scene.nodes[2].local.translation.z = 1.2f;
  scene.update_transforms();
  place_section(mode, scene, 0.0f);
  scene.nodes[2].ghosted = true;
  mode.ghost_opacity = 0.8f;
  const QImage ghost_reference = uncapped();
  check_cap_blend(draw(), ghost_reference, cap, mode.section_cap_color,
                  "translucent cap must blend after rear isolate ghosts");
  scene.nodes[2].ghosted = false;
  scene.nodes[2].local.translation.z = 0.0f;
  scene.update_transforms();
  place_section(mode, scene, 0.0f);

  mode.hidden_line = true;
  const QImage paper = draw();
  mode.section_translucent = false;
  check_patch_equal(paper, draw(), cap,
                     "hidden-line paper must stay opaque regardless of translucency preference");

  mode.hidden_line = false;
  mode.section_translucent = true;
  scene.nodes[2].visible = false;
  scene.camera.orientation = glm::angleAxis(glm::pi<float>(), s::vec3(0.0f, 1.0f, 0.0f));
  const QPoint back = project(scene.camera, {-1.5f, 1.0f, -0.5f});
  const QImage back_cut = draw();
  mode.section_enabled = false;
  check_patch_equal(back_cut, draw(), back,
                     "retained outer surfaces must occlude translucent caps from behind");
}

void verify_hollow(r::IRenderer& renderer, QOpenGLFramebufferObject& target,
                   s::Scene& scene, int samples, s::Projection projection) {
  std::fprintf(stdout, "Hollow section pixels: MSAA %d, %s\n", samples,
               projection == s::Projection::Orthographic ? "ortho" : "perspective");
  scene.camera.projection_mode = projection;
  scene.camera.orientation = glm::angleAxis(glm::radians(20.0f), s::vec3(0, 1, 0)) *
                             glm::angleAxis(glm::radians(-15.0f), s::vec3(1, 0, 0));
  r::DisplayMode mode;
  mode.msaa_samples = samples;
  mode.show_axes = mode.show_scale_bar = mode.show_edges = false;
  mode.section_enabled = true;
  mode.section_show_plane = false;
  place_section(mode, scene, 0.0f);
  const QPoint cavity = project(scene.camera, {0.0f, 0.6f, 0.0f});
  const QPoint wall = project(scene.camera, {2.15f, 0.6f, 0.0f});
  auto draw = [&]() {
    target.bind();
    renderer.render(mode);
    return target.toImage();
  };
  const QImage translucent = draw();
  mode.section_translucent = false;
  const QImage opaque = draw();
  // Marking the mesh open disables capping and gives an independent reference
  // for the actual inner wall seen through the cavity.
  scene.meshes[0]->double_sided = true;
  const QImage open = draw();
  scene.meshes[0]->double_sided = false;
  check_patch_equal(opaque, open, cavity, "opaque hatch must leave a hollow solid's cavity open");
  check_patch_equal(translucent, open, cavity,
                     "translucent hatch must leave a hollow solid's cavity open");
  check(patch_distance(opaque, translucent, wall) > 30,
        "hollow solid's material must still receive the selected cap style");

  const QString capture_dir = qEnvironmentVariable("CADLY_SECTION_TEST_CAPTURE_DIR");
  if (!capture_dir.isEmpty() && samples == 4 && projection == s::Projection::Perspective) {
    check(QDir().mkpath(capture_dir), "create section capture directory");
    check(opaque.save(QDir(capture_dir).filePath("section-hollow-opaque.png")), "save opaque section");
    check(translucent.save(QDir(capture_dir).filePath("section-hollow-translucent.png")),
          "save translucent section");
  }

  for (float z : {0.8f, -0.8f, -1.25f}) {
    place_section(mode, scene, z);
    const QPoint centre = project(scene.camera, {0.0f, 0.0f, z});
    scene.meshes[0]->double_sided = true;
    const QImage reference = draw();
    scene.meshes[0]->double_sided = false;
    mode.section_translucent = false;
    const QImage solid_cut = draw();
    mode.section_translucent = true;
    const QImage transparent_cut = draw();
    if (z > -1.0f) {
      check_patch_equal(solid_cut, reference, centre,
                         "moving the plane inside a cavity must not fill the opening");
      check_patch_equal(transparent_cut, reference, centre,
                         "deep translucent sections must preserve the actual inner wall");
    } else {
      check(patch_distance(solid_cut, reference, centre) > 30,
            "a section below the cavity floor must cap the remaining material");
      mode.section_hatch = false;
      check_cap_blend(draw(), reference, centre, mode.section_cap_color,
                      "deep section fill must blend over a visible back wall");
      mode.section_hatch = true;
    }
  }

  place_section(mode, scene, 0.0f);
  mode.section_translucent = false;
  s::Node reflected = scene.nodes[0];
  reflected.local.scale.x = -1.0f;
  scene.add_node(std::move(reflected));
  scene.update_transforms();
  check_patch_equal(opaque, draw(), wall,
                     "overlapping reflected instances must not cancel a section cap");
  scene.nodes.pop_back();
  scene.update_transforms();
}

void verify_backfaces(r::IRenderer& renderer, QOpenGLFramebufferObject& target,
                      s::Scene& scene, int samples, s::Projection projection) {
  std::fprintf(stdout, "Back-face pixels: MSAA %d, %s\n", samples,
               projection == s::Projection::Orthographic ? "ortho" : "perspective");
  scene.camera.projection_mode = projection;
  r::DisplayMode mode;
  mode.msaa_samples = samples;
  mode.show_axes = mode.show_scale_bar = mode.show_edges = false;
  mode.section_show_plane = false;
  const QPoint centre(kSize / 2, kSize / 2);
  auto draw = [&]() {
    target.bind();
    renderer.render(mode);
    return target.toImage();
  };
  const s::quat front_view(1.0f, 0.0f, 0.0f, 0.0f);
  const s::quat back_view = glm::angleAxis(glm::pi<float>(), s::vec3(0, 1, 0));
  QImage front, back;
  for (float scale : {1.0f, -1.0f}) {
    scene.nodes[0].local.scale.x = scale;
    scene.update_transforms();
    scene.camera.orientation = front_view;
    const QImage front_face = draw();
    scene.camera.orientation = back_view;
    const QImage back_face = draw();
    if (scale > 0.0f) {
      front = front_face;
      back = back_face;
    } else {
      check_patch_equal(front, front_face, centre,
                         "reflection must preserve the surface's front material");
      check_patch_equal(back, back_face, centre,
                         "reflection must preserve the surface's back material");
    }
  }
  check(qRed(front.pixel(centre)) > qBlue(front.pixel(centre)) + 30,
        "open surface front must retain its warm imported material");
  check(qBlue(back.pixel(centre)) > qRed(back.pixel(centre)) + 15,
        "open surface back must use a visible cool inspection material");
  check(qAlpha(back.pixel(centre)) == 255, "back-face material must remain opaque");

  const s::Material original = scene.materials[0];
  scene.materials[0].metallic = 1.0f;
  scene.materials[0].roughness = 0.05f;
  scene.materials[0].emissive = 1.0f;
  scene.materials[0].emissive_color = s::vec3(1.0f);
  check_patch_equal(back, draw(), centre,
                     "front-side metalness and emission must not wash out the back-face cue");
  scene.materials[0] = original;

  mode.backface_color = {0.8f, 0.25f, 0.2f};
  check(patch_distance(back, draw(), centre) > 40,
        "back-face theme color must reach the surface shader");
  scene.camera.orientation = front_view;
  check_patch_equal(front, draw(), centre, "back-face theme color must not tint the front");
  mode.backface_color = r::DisplayMode{}.backface_color;
  scene.camera.orientation = back_view;

  scene.nodes[0].selected = true;
  check(patch_distance(back, draw(), centre) > 40,
        "selection highlight must still cover the back-face material");
  scene.nodes[0].selected = false;
  scene.nodes[0].ghosted = true;
  const QImage ghost = draw();
  check(patch_distance(back, ghost, centre) > 20 && qAlpha(ghost.pixel(centre)) == 255,
        "ghosted back faces must fade while preserving window opacity");
  scene.nodes[0].ghosted = false;

  // Clip an open sheet across its width. Only actual surviving surface may
  // render; the source mesh must not become cap-eligible to show its back.
  mode.section_enabled = true;
  mode.section.normal = {1.0f, 0.0f, 0.0f};
  mode.section.offset = 0.0f;
  const QImage clipped = draw();
  const QPoint kept = project(scene.camera, {0.6f, 0.4f, 0.0f});
  const QPoint removed = project(scene.camera, {-0.6f, 0.4f, 0.0f});
  check_patch_equal(clipped, back, kept, "sectioned open sheet must retain its back-face shading");
  scene.nodes[0].visible = false;
  check_patch_equal(clipped, draw(), removed, "removed sheet must leave no phantom cap");
  scene.nodes[0].visible = true;

  const QString capture_dir = qEnvironmentVariable("CADLY_SECTION_TEST_CAPTURE_DIR");
  if (!capture_dir.isEmpty() && samples == 4 && projection == s::Projection::Perspective) {
    check(QDir().mkpath(capture_dir), "create back-face capture directory");
    check(front.save(QDir(capture_dir).filePath("surface-front.png")), "save front surface");
    check(back.save(QDir(capture_dir).filePath("surface-back.png")), "save back surface");
  }
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
      verify_translucent(*renderer, target, *scene, samples, projection);
    }
  }

  auto hollow = std::make_shared<s::Scene>();
  hollow->materials.resize(2);
  hollow->materials[1].base_color = s::vec4(0.16f, 0.52f, 0.72f, 1.0f);
  auto shell = std::make_shared<s::Mesh>();
  append_box(*shell, {-2.5f, -2.0f, -1.6f}, {2.5f, 2.0f, 1.6f}, 0);
  append_box(*shell, {-1.7f, -1.2f, -1.0f}, {1.7f, 1.2f, 1.0f}, 1, true);
  s::Node hollow_node;
  hollow_node.mesh_index = hollow->add_mesh(shell);
  hollow->add_node(std::move(hollow_node));
  hollow->update_transforms();
  hollow->camera.target = s::vec3(0.0f);
  hollow->camera.distance = 8.0f;
  renderer->attach_scene(hollow);
  for (int samples : {0, 4}) {
    for (s::Projection projection : {s::Projection::Orthographic,
                                     s::Projection::Perspective}) {
      verify_hollow(*renderer, target, *hollow, samples, projection);
    }
  }

  auto sheet = std::make_shared<s::Scene>();
  sheet->materials.resize(1);
  sheet->materials[0].base_color = s::vec4(0.6f, 0.22f, 0.08f, 1.0f);
  auto surface_mesh = std::make_shared<s::Mesh>();
  surface_mesh->double_sided = true;
  for (const s::vec3& p : {s::vec3(-1.5f, -1.5f, 0.0f), s::vec3(1.5f, -1.5f, 0.0f),
                           s::vec3(1.5f, 1.5f, 0.0f), s::vec3(-1.5f, 1.5f, 0.0f)}) {
    surface_mesh->vertices.push_back({p, {0.0f, 0.0f, 1.0f}});
    surface_mesh->bounds.expand(p);
  }
  surface_mesh->indices = {0, 1, 2, 0, 2, 3};
  s::Submesh surface_sub;
  surface_sub.index_count = 6;
  surface_sub.bounds = surface_mesh->bounds;
  surface_mesh->submeshes.push_back(surface_sub);
  s::Node surface_node;
  surface_node.mesh_index = sheet->add_mesh(surface_mesh);
  sheet->add_node(std::move(surface_node));
  sheet->update_transforms();
  sheet->camera.target = s::vec3(0.0f);
  sheet->camera.distance = 5.0f;
  renderer->attach_scene(sheet);
  for (int samples : {0, 4}) {
    for (s::Projection projection : {s::Projection::Orthographic,
                                     s::Projection::Perspective}) {
      verify_backfaces(*renderer, target, *sheet, samples, projection);
    }
  }

  auto coplanar = std::make_shared<s::Scene>();
  coplanar->materials.resize(1);
  add_box(*coplanar, {-2.5f, -2.0f, -1.0f}, {2.5f, 2.0f, 0.0f}, 0);
  add_box(*coplanar, {-1.5f, -1.5f, -1.0f}, {1.5f, 1.5f, 1.0f}, 0);
  coplanar->update_transforms();
  coplanar->camera.target = s::vec3(0.0f);
  coplanar->camera.distance = 8.0f;
  renderer->attach_scene(coplanar);
  for (int samples : {0, 4}) {
    for (s::Projection projection : {s::Projection::Orthographic,
                                     s::Projection::Perspective}) {
      verify_coplanar_cap(*renderer, target, *coplanar, samples, projection);
    }
  }

  auto touching = std::make_shared<s::Scene>();
  touching->materials.resize(1);
  add_box(*touching, {-2.5f, -2.0f, -0.5f}, {2.5f, 2.0f, 0.5f}, 0);
  add_box(*touching, {-1.5f, -1.2f, -1.5f}, {1.5f, 1.2f, -0.5f}, 0);
  // A different, thin triangulation on the contacting front face exercises
  // raster depth-slope rounding, as seen around bolt holes in CAD meshes.
  auto& rear = *touching->meshes[1];
  rear.indices.resize(rear.indices.size() - 6);
  for (int strip = 0; strip < 256; ++strip) {
    const float lo = -1.2f + 2.4f * static_cast<float>(strip) / 256.0f;
    const float hi = -1.2f + 2.4f * static_cast<float>(strip + 1) / 256.0f;
    const auto first = static_cast<std::uint32_t>(rear.vertices.size());
    for (const s::vec3& p : {s::vec3(-1.5f, lo, -0.5f), s::vec3(1.5f, lo, -0.5f),
                             s::vec3(1.5f, hi, -0.5f), s::vec3(-1.5f, hi, -0.5f)}) {
      rear.vertices.push_back({p, {0.0f, 0.0f, 1.0f}});
    }
    for (std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) rear.indices.push_back(first + i);
  }
  rear.submeshes[0].index_count = static_cast<std::uint32_t>(rear.indices.size());
  for (auto& node : touching->nodes) node.local.translation = {100.0f, 75.0f, 0.0f};
  touching->update_transforms();
  touching->camera.target = {100.0f, 75.0f, 0.0f};
  touching->camera.distance = 8.0f;
  touching->camera.far_z = 40.0f;
  renderer->attach_scene(touching);
  for (int samples : {0, 4}) {
    for (s::Projection projection : {s::Projection::Orthographic,
                                     s::Projection::Perspective}) {
      verify_touching_solids(*renderer, target, *touching, samples, projection);
    }
  }
  renderer->shutdown();
  check(context.functions()->glGetError() == GL_NO_ERROR,
        "section rendering must not produce OpenGL errors");
  return failures == 0 ? 0 : 1;
}

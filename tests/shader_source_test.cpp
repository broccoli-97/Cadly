#include "GLShader.h"

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <string>

int main() {
#ifdef _WIN32
  _putenv_s("CADLY_ASSET_ROOT", CADLY_TEST_SOURCE_ROOT);
#else
  setenv("CADLY_ASSET_ROOT", CADLY_TEST_SOURCE_ROOT, 1);
#endif

  const auto pbr = cadly::renderer_gl::detail::load_shader_source("pbr.vert");
  assert(pbr);
  assert(pbr->find("#include") == std::string::npos);
  assert(pbr->find("layout(std140) uniform FrameBlock") != std::string::npos);

  const auto pbr_frag =
    cadly::renderer_gl::detail::load_shader_source("pbr.frag");
  assert(pbr_frag);
  assert(pbr_frag->find("u_view_ref.w > 0.5") != std::string::npos);
  assert(pbr_frag->find("normalize(u_view_ref.xyz - v_world_pos)") !=
         std::string::npos);
  assert(pbr_frag->find(": u_view_ref.xyz") != std::string::npos);
  assert(pbr_frag->find("u_camera_pos") == std::string::npos);
  assert(pbr_frag->find("mat3(u_view)") != std::string::npos);
  assert(pbr_frag->find("world_to_camera * N") != std::string::npos);
  assert(pbr_frag->find("world_to_camera * R") != std::string::npos);

  const auto fullscreen =
    cadly::renderer_gl::detail::load_shader_source("prefilter.vert");
  assert(fullscreen);
  assert(fullscreen->find("fullscreen_triangle_position()") !=
         std::string::npos);
  assert(fullscreen->find("#include") == std::string::npos);

  // Section view: the clip must reach EVERY stage that draws model geometry.
  // Miss one and the model is cut in one pass and whole in another — surfaces
  // sliced but their edges still hanging in the air, or worse, the stencil
  // counting pass (which borrows the edges program) seeing unclipped geometry,
  // where every closed solid balances and no cap is ever produced. These
  // source checks cover the clip writes in all model stages alongside the
  // framebuffer compositing checks in section_render_test.
  //
  // Match the ASSIGNMENT, not the bare identifier: these shaders discuss
  // gl_ClipDistance at length in their comments, and a check that counted prose
  // would pass on a shader that only talks about clipping.
  const std::string kWrite = "gl_ClipDistance[0] =";
  for (const char* stage : {"pbr.vert", "edges.vert", "silhouette.geom"}) {
    const auto src = cadly::renderer_gl::detail::load_shader_source(stage);
    assert(src);
    assert(src->find(kWrite) != std::string::npos);
    assert(src->find("u_clip_plane") != std::string::npos);
  }
  // The geometry stage must re-set it before EACH EmitVertex: outputs become
  // undefined after one, so a single assignment silently clips only half the
  // silhouette segments.
  const auto silhouette =
    cadly::renderer_gl::detail::load_shader_source("silhouette.geom");
  assert(silhouette);
  {
    std::size_t count = 0;
    for (std::size_t at = silhouette->find(kWrite);
         at != std::string::npos;
         at = silhouette->find(kWrite, at + 1)) {
      ++count;
    }
    assert(count >= 2);
  }
  // The section pass draws ON its own plane, where the clip distance is ~0 and
  // the comparison is a coin flip, so it must never write one.
  const auto section =
    cadly::renderer_gl::detail::load_shader_source("section.vert");
  assert(section);
  assert(section->find(kWrite) == std::string::npos);
  return 0;
}

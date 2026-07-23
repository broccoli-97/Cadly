#include "GLShader.h"

#include <cassert>
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
  assert(pbr_frag->find("mat3(u_view)") != std::string::npos);
  assert(pbr_frag->find("world_to_camera * N") != std::string::npos);
  assert(pbr_frag->find("world_to_camera * R") != std::string::npos);

  const auto fullscreen =
    cadly::renderer_gl::detail::load_shader_source("prefilter.vert");
  assert(fullscreen);
  assert(fullscreen->find("fullscreen_triangle_position()") !=
         std::string::npos);
  assert(fullscreen->find("#include") == std::string::npos);
  return 0;
}

#include "cadly/renderer_gl/GLRenderer.h"

#include "GLShader.h"

#include "cadly/platform/Log.h"
#include "cadly/scene/Math.h"
#include "cadly/scene/Mesh.h"
#include "cadly/scene/Node.h"
#include "cadly/scene/Scene.h"

#include <QOpenGLFunctions_4_1_Core>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <unordered_map>

namespace cadly::renderer_gl {

namespace {

using detail::GLProgram;
using detail::load_shader_source;

// std140 packed frame uniform. Matches the FrameBlock declared in the shaders.
struct alignas(16) FrameBlock {
  scene::mat4 view;
  scene::mat4 proj;
  scene::mat4 view_proj;
  scene::vec4 camera_pos;
  scene::vec4 ambient;
  scene::vec4 key_dir;
  scene::vec4 key_color;
  scene::vec4 fill_dir;
  scene::vec4 fill_color;
  scene::vec4 rim_dir;
  scene::vec4 rim_color;
};

// One per scene::Mesh. Cached so re-uploading happens only when the source
// pointer changes.
struct MeshGpu {
  GLuint vao{0};
  GLuint vbo{0};
  GLuint ibo{0};
  GLsizei index_count{0};
  // BRep edge buffers (optional). Allocated only if the source mesh has
  // edge polylines from the importer.
  GLuint edge_vao{0};
  GLuint edge_vbo{0};
  GLuint edge_ibo{0};
  GLsizei edge_index_count{0};
  std::shared_ptr<scene::Mesh> source; // keeps the CPU data alive
};

// Implementation -----------------------------------------------------------
class GLRendererImpl final : public renderer::IRenderer {
public:
  GLRendererImpl() = default;
  ~GLRendererImpl() override { /* shutdown() handles cleanup explicitly */ }

  void initialize() override;
  void resize(int width_px, int height_px) override;
  void attach_scene(std::shared_ptr<scene::Scene> scene) override;
  void render(const renderer::DisplayMode& mode) override;
  scene::SelectionId pick(int, int) override { return {}; }
  void shutdown() override;

private:
  bool build_programs();
  void ensure_mesh_upload(const scene::Mesh& mesh,
                          const std::shared_ptr<scene::Mesh>& mesh_ptr);
  void update_frame_uniforms();
  void draw_background(const renderer::DisplayMode& mode);
  void draw_grid(const renderer::DisplayMode& mode);
  void draw_pivot(const renderer::DisplayMode& mode);
  void draw_edges(const renderer::DisplayMode& mode);

  // IBL bake — runs once at init. The four targets together implement the
  // Karis split-sum approximation: irradiance for diffuse, prefilter +
  // brdf_lut for specular.
  bool   bake_ibl();
  GLuint create_cubemap(int size, int mip_count);
  void   bake_env_cube();
  void   bake_irradiance();
  void   bake_prefilter();
  void   bake_brdf_lut();
  static scene::mat3 cube_face_basis(int face);

  QOpenGLFunctions_4_1_Core gl_;
  bool   initialised_{false};
  GLint  viewport_w_{1};
  GLint  viewport_h_{1};

  // Programs.
  GLProgram prog_pbr_;
  GLProgram prog_background_;
  GLProgram prog_grid_;
  GLProgram prog_pivot_;
  GLProgram prog_edges_;
  GLProgram prog_env_capture_;
  GLProgram prog_irradiance_;
  GLProgram prog_prefilter_;
  GLProgram prog_brdf_lut_;

  // Frame UBO.
  GLuint   ubo_frame_{0};
  GLuint   frame_binding_{0};

  // Background VAO (no buffer; uses gl_VertexID).
  GLuint   vao_background_{0};

  // Grid quad.
  GLuint   vao_grid_{0};
  GLuint   vbo_grid_{0};

  // Rotation-pivot indicator (empty VAO; uses gl_VertexID).
  GLuint   vao_pivot_{0};

  // IBL bake state. Cubemaps stay resident for the life of the renderer;
  // the FBO + depth RBO are reused across the bake passes and freed at the
  // end of `bake_ibl`.
  GLuint   env_cube_{0};
  GLuint   irradiance_cube_{0};
  GLuint   prefilter_cube_{0};
  GLuint   brdf_lut_{0};
  int      env_cube_size_{128};
  int      irradiance_size_{32};
  int      prefilter_size_{128};
  int      prefilter_mip_count_{5};
  int      brdf_lut_size_{256};
  GLuint   vao_quad_{0};

  // Scene + GPU mesh cache.
  std::shared_ptr<scene::Scene> scene_;
  std::unordered_map<scene::Mesh*, MeshGpu> meshes_;
};

void GLRendererImpl::initialize() {
  if (initialised_) return;
  if (!gl_.initializeOpenGLFunctions()) {
    CADLY_LOG_ERROR("Failed to load OpenGL 4.1 core functions. "
                    "Is the context current and a compatible version?");
    return;
  }

  gl_.glEnable(GL_DEPTH_TEST);
  gl_.glDepthFunc(GL_LEQUAL);
  gl_.glEnable(GL_CULL_FACE);
  gl_.glCullFace(GL_BACK);
  gl_.glFrontFace(GL_CCW);
  gl_.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  if (!build_programs()) {
    CADLY_LOG_ERROR("Renderer initialisation failed; shaders did not compile.");
    return;
  }

  // Frame UBO.
  gl_.glGenBuffers(1, &ubo_frame_);
  gl_.glBindBuffer(GL_UNIFORM_BUFFER, ubo_frame_);
  gl_.glBufferData(GL_UNIFORM_BUFFER, sizeof(FrameBlock), nullptr, GL_DYNAMIC_DRAW);
  gl_.glBindBuffer(GL_UNIFORM_BUFFER, 0);
  gl_.glBindBufferBase(GL_UNIFORM_BUFFER, frame_binding_, ubo_frame_);

  // Bind FrameBlock from each program to the same binding point.
  for (GLProgram* p : {&prog_pbr_, &prog_edges_}) {
    if (!p->valid()) continue;
    GLuint idx = p->uniform_block(gl_, "FrameBlock");
    if (idx != GL_INVALID_INDEX) {
      gl_.glUniformBlockBinding(p->id(), idx, frame_binding_);
    }
  }

  // Background "draw a giant triangle from gl_VertexID" VAO — empty layout,
  // but core profile still needs a bound VAO to call glDrawArrays.
  gl_.glGenVertexArrays(1, &vao_background_);

  // Grid fullscreen quad (two triangles in NDC).
  const std::array<float, 18> grid_verts = {
    -1.f, -1.f, 0.f,  1.f, -1.f, 0.f,  1.f,  1.f, 0.f,
    -1.f, -1.f, 0.f,  1.f,  1.f, 0.f, -1.f,  1.f, 0.f,
  };
  gl_.glGenVertexArrays(1, &vao_grid_);
  gl_.glGenBuffers(1, &vbo_grid_);
  gl_.glBindVertexArray(vao_grid_);
  gl_.glBindBuffer(GL_ARRAY_BUFFER, vbo_grid_);
  gl_.glBufferData(GL_ARRAY_BUFFER, sizeof(grid_verts), grid_verts.data(), GL_STATIC_DRAW);
  gl_.glEnableVertexAttribArray(0);
  gl_.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
  gl_.glBindVertexArray(0);

  // Pivot indicator: empty VAO, geometry emitted from gl_VertexID.
  gl_.glGenVertexArrays(1, &vao_pivot_);

  // Dummy VAO for the IBL bake passes — they draw a single fullscreen
  // triangle with no vertex attributes and pull positions from gl_VertexID.
  gl_.glGenVertexArrays(1, &vao_quad_);

  // Bake the IBL targets once. Failure is non-fatal (the PBR shader falls
  // back to its constant ambient if the cubemaps are zero) but the user
  // would lose all environment lighting, so log loudly.
  if (!bake_ibl()) {
    CADLY_LOG_WARN("IBL bake failed — falling back to analytical ambient.");
  }

  initialised_ = true;
  CADLY_LOG_INFO("OpenGL renderer initialised: GL_VERSION='{}'",
                 reinterpret_cast<const char*>(gl_.glGetString(GL_VERSION)));
}

bool GLRendererImpl::build_programs() {
  bool all_ok = true;

  auto build = [&](GLProgram& prog,
                   const char* vert_name,
                   const char* frag_name,
                   const char* debug_name) {
    auto vs = load_shader_source(vert_name);
    auto fs = load_shader_source(frag_name);
    if (!vs || !fs) {
      CADLY_LOG_ERROR("Missing shader source for {}", debug_name);
      all_ok = false;
      return;
    }
    if (!prog.build(gl_, *vs, *fs, debug_name)) {
      all_ok = false;
    }
  };

  build(prog_pbr_,         "pbr.vert",         "pbr.frag",         "pbr");
  build(prog_background_,  "background.vert",  "background.frag",  "background");
  build(prog_grid_,        "grid.vert",        "grid.frag",        "grid");
  build(prog_pivot_,       "pivot.vert",       "pivot.frag",       "pivot");
  build(prog_edges_,       "edges.vert",       "edges.frag",       "edges");
  build(prog_env_capture_, "env_capture.vert", "env_capture.frag", "env_capture");
  build(prog_irradiance_,  "irradiance.vert",  "irradiance.frag",  "irradiance");
  build(prog_prefilter_,   "prefilter.vert",   "prefilter.frag",   "prefilter");
  build(prog_brdf_lut_,    "brdf_lut.vert",    "brdf_lut.frag",    "brdf_lut");
  return all_ok;
}

void GLRendererImpl::resize(int width_px, int height_px) {
  viewport_w_ = std::max(1, width_px);
  viewport_h_ = std::max(1, height_px);
}

void GLRendererImpl::attach_scene(std::shared_ptr<scene::Scene> scene) {
  scene_ = std::move(scene);
  // We do not invalidate the mesh cache: existing MeshGpu entries are keyed
  // by raw Mesh* and the importer hands us new pointers, so stale entries are
  // simply unused. shutdown() will reclaim them.
}

void GLRendererImpl::ensure_mesh_upload(const scene::Mesh& mesh,
                                        const std::shared_ptr<scene::Mesh>& mesh_ptr) {
  auto it = meshes_.find(const_cast<scene::Mesh*>(&mesh));
  if (it != meshes_.end()) return;

  if (mesh.vertices.empty() || mesh.indices.empty()) return;

  MeshGpu g;
  g.source = mesh_ptr;
  g.index_count = static_cast<GLsizei>(mesh.indices.size());

  gl_.glGenVertexArrays(1, &g.vao);
  gl_.glGenBuffers(1, &g.vbo);
  gl_.glGenBuffers(1, &g.ibo);

  gl_.glBindVertexArray(g.vao);

  gl_.glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
  gl_.glBufferData(GL_ARRAY_BUFFER,
                   static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(scene::Vertex)),
                   mesh.vertices.data(),
                   GL_STATIC_DRAW);

  gl_.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ibo);
  gl_.glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                   static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(std::uint32_t)),
                   mesh.indices.data(),
                   GL_STATIC_DRAW);

  // attribute 0: position (vec3)
  gl_.glEnableVertexAttribArray(0);
  gl_.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                            sizeof(scene::Vertex),
                            reinterpret_cast<void*>(offsetof(scene::Vertex, position)));
  // attribute 1: normal (vec3)
  gl_.glEnableVertexAttribArray(1);
  gl_.glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                            sizeof(scene::Vertex),
                            reinterpret_cast<void*>(offsetof(scene::Vertex, normal)));
  // attribute 2: rgba8 colour, unpacked to normalised vec4 by the driver.
  gl_.glEnableVertexAttribArray(2);
  gl_.glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                            sizeof(scene::Vertex),
                            reinterpret_cast<void*>(offsetof(scene::Vertex, color_rgba8)));

  gl_.glBindVertexArray(0);

  // Edge polylines, if the importer captured any. Vertex layout is a flat
  // packed array of vec3 positions; indices come in GL_LINES pairs.
  if (!mesh.edge_vertices.empty() && !mesh.edge_indices.empty()) {
    gl_.glGenVertexArrays(1, &g.edge_vao);
    gl_.glGenBuffers(1, &g.edge_vbo);
    gl_.glGenBuffers(1, &g.edge_ibo);
    gl_.glBindVertexArray(g.edge_vao);
    gl_.glBindBuffer(GL_ARRAY_BUFFER, g.edge_vbo);
    gl_.glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(mesh.edge_vertices.size() *
                                             sizeof(scene::vec3)),
                     mesh.edge_vertices.data(),
                     GL_STATIC_DRAW);
    gl_.glEnableVertexAttribArray(0);
    gl_.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                              sizeof(scene::vec3), nullptr);
    gl_.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.edge_ibo);
    gl_.glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(mesh.edge_indices.size() *
                                             sizeof(std::uint32_t)),
                     mesh.edge_indices.data(),
                     GL_STATIC_DRAW);
    g.edge_index_count = static_cast<GLsizei>(mesh.edge_indices.size());
    gl_.glBindVertexArray(0);
  }

  meshes_.emplace(const_cast<scene::Mesh*>(&mesh), g);
}

// IBL bake ------------------------------------------------------------------

scene::mat3 GLRendererImpl::cube_face_basis(int face) {
  // Columns are (right, up, fwd) such that a clipspace vertex (x,y) on the
  // fullscreen triangle maps to world-space sample direction
  //     dir = right * x + up * y + fwd
  // matching the OpenGL cubemap convention (RenderMan: Y-down within faces).
  switch (face) {
    case 0: return scene::mat3(scene::vec3( 0, 0,-1), scene::vec3(0,-1, 0), scene::vec3( 1, 0, 0)); // +X
    case 1: return scene::mat3(scene::vec3( 0, 0, 1), scene::vec3(0,-1, 0), scene::vec3(-1, 0, 0)); // -X
    case 2: return scene::mat3(scene::vec3( 1, 0, 0), scene::vec3(0, 0, 1), scene::vec3( 0, 1, 0)); // +Y
    case 3: return scene::mat3(scene::vec3( 1, 0, 0), scene::vec3(0, 0,-1), scene::vec3( 0,-1, 0)); // -Y
    case 4: return scene::mat3(scene::vec3( 1, 0, 0), scene::vec3(0,-1, 0), scene::vec3( 0, 0, 1)); // +Z
    case 5: return scene::mat3(scene::vec3(-1, 0, 0), scene::vec3(0,-1, 0), scene::vec3( 0, 0,-1)); // -Z
  }
  return scene::mat3(1.0f);
}

GLuint GLRendererImpl::create_cubemap(int size, int mip_count) {
  GLuint tex = 0;
  gl_.glGenTextures(1, &tex);
  gl_.glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
  for (int face = 0; face < 6; ++face) {
    // glTexImage2D once per face, then mips are allocated by glGenerateMipmap
    // (or by us via subsequent glTexImage2D calls per level for prefilter).
    gl_.glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0,
                     GL_RGBA16F, size, size, 0, GL_RGBA, GL_FLOAT, nullptr);
  }
  gl_.glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl_.glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  gl_.glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  gl_.glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
                      mip_count > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
  gl_.glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  if (mip_count > 1) {
    // Allocate the full mip chain up front so glFramebufferTexture2D can
    // attach any level; the prefilter pass writes them explicitly.
    gl_.glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, 0);
    gl_.glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, mip_count - 1);
    int level_size = size;
    for (int level = 1; level < mip_count; ++level) {
      level_size = std::max(1, level_size / 2);
      for (int face = 0; face < 6; ++face) {
        gl_.glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, level,
                         GL_RGBA16F, level_size, level_size, 0,
                         GL_RGBA, GL_FLOAT, nullptr);
      }
    }
  }
  gl_.glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
  return tex;
}

bool GLRendererImpl::bake_ibl() {
  if (!prog_env_capture_.valid() || !prog_irradiance_.valid() ||
      !prog_prefilter_.valid()  || !prog_brdf_lut_.valid()) {
    return false;
  }

  // Snapshot GL state we're going to clobber so the caller's setup survives.
  GLint prev_fbo = 0, prev_viewport[4] = {0, 0, 0, 0};
  gl_.glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
  gl_.glGetIntegerv(GL_VIEWPORT, prev_viewport);

  // env_cube needs a mip chain because the prefilter pass samples it with
  // textureLod at PDF-matched mip levels to dodge fireflies.
  const int env_mip_count = static_cast<int>(std::floor(std::log2(env_cube_size_))) + 1;
  env_cube_        = create_cubemap(env_cube_size_,   env_mip_count);
  irradiance_cube_ = create_cubemap(irradiance_size_, 1);
  prefilter_cube_  = create_cubemap(prefilter_size_,  prefilter_mip_count_);

  gl_.glGenTextures(1, &brdf_lut_);
  gl_.glBindTexture(GL_TEXTURE_2D, brdf_lut_);
  gl_.glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, brdf_lut_size_, brdf_lut_size_,
                   0, GL_RG, GL_FLOAT, nullptr);
  gl_.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl_.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  gl_.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl_.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl_.glBindTexture(GL_TEXTURE_2D, 0);

  gl_.glDisable(GL_DEPTH_TEST);
  gl_.glDisable(GL_CULL_FACE);
  gl_.glDisable(GL_BLEND);
  gl_.glBindVertexArray(vao_quad_);

  bake_env_cube();

  // Generate mip chain of env cube — the prefilter shader samples mip levels
  // via textureLod to keep importance-sampled specular free of fireflies.
  gl_.glBindTexture(GL_TEXTURE_CUBE_MAP, env_cube_);
  gl_.glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
  gl_.glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

  bake_irradiance();
  bake_prefilter();
  bake_brdf_lut();

  gl_.glBindVertexArray(0);
  gl_.glEnable(GL_DEPTH_TEST);
  gl_.glEnable(GL_CULL_FACE);
  gl_.glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev_fbo));
  gl_.glViewport(prev_viewport[0], prev_viewport[1],
                 prev_viewport[2], prev_viewport[3]);
  return true;
}

void GLRendererImpl::bake_env_cube() {
  GLuint fbo = 0;
  gl_.glGenFramebuffers(1, &fbo);
  gl_.glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  gl_.glViewport(0, 0, env_cube_size_, env_cube_size_);
  gl_.glUseProgram(prog_env_capture_.id());
  const GLint loc_basis = prog_env_capture_.uniform(gl_, "u_face_basis");

  for (int face = 0; face < 6; ++face) {
    gl_.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                               env_cube_, 0);
    const scene::mat3 basis = cube_face_basis(face);
    gl_.glUniformMatrix3fv(loc_basis, 1, GL_FALSE, glm::value_ptr(basis));
    gl_.glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  gl_.glBindFramebuffer(GL_FRAMEBUFFER, 0);
  gl_.glDeleteFramebuffers(1, &fbo);
}

void GLRendererImpl::bake_irradiance() {
  GLuint fbo = 0;
  gl_.glGenFramebuffers(1, &fbo);
  gl_.glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  gl_.glViewport(0, 0, irradiance_size_, irradiance_size_);

  gl_.glUseProgram(prog_irradiance_.id());
  const GLint loc_basis = prog_irradiance_.uniform(gl_, "u_face_basis");
  const GLint loc_env   = prog_irradiance_.uniform(gl_, "u_env_cube");
  gl_.glUniform1i(loc_env, 0);
  gl_.glActiveTexture(GL_TEXTURE0);
  gl_.glBindTexture(GL_TEXTURE_CUBE_MAP, env_cube_);

  for (int face = 0; face < 6; ++face) {
    gl_.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                               irradiance_cube_, 0);
    const scene::mat3 basis = cube_face_basis(face);
    gl_.glUniformMatrix3fv(loc_basis, 1, GL_FALSE, glm::value_ptr(basis));
    gl_.glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  gl_.glBindFramebuffer(GL_FRAMEBUFFER, 0);
  gl_.glDeleteFramebuffers(1, &fbo);
}

void GLRendererImpl::bake_prefilter() {
  GLuint fbo = 0;
  gl_.glGenFramebuffers(1, &fbo);
  gl_.glBindFramebuffer(GL_FRAMEBUFFER, fbo);

  gl_.glUseProgram(prog_prefilter_.id());
  const GLint loc_basis = prog_prefilter_.uniform(gl_, "u_face_basis");
  const GLint loc_env   = prog_prefilter_.uniform(gl_, "u_env_cube");
  const GLint loc_rough = prog_prefilter_.uniform(gl_, "u_roughness");
  const GLint loc_res   = prog_prefilter_.uniform(gl_, "u_env_resolution");
  gl_.glUniform1i(loc_env, 0);
  gl_.glUniform1f(loc_res, static_cast<float>(env_cube_size_));
  gl_.glActiveTexture(GL_TEXTURE0);
  gl_.glBindTexture(GL_TEXTURE_CUBE_MAP, env_cube_);

  for (int mip = 0; mip < prefilter_mip_count_; ++mip) {
    const int mip_size = std::max(1, prefilter_size_ >> mip);
    gl_.glViewport(0, 0, mip_size, mip_size);
    const float roughness = prefilter_mip_count_ > 1
      ? static_cast<float>(mip) / static_cast<float>(prefilter_mip_count_ - 1)
      : 0.0f;
    gl_.glUniform1f(loc_rough, roughness);
    for (int face = 0; face < 6; ++face) {
      gl_.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                 GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                                 prefilter_cube_, mip);
      const scene::mat3 basis = cube_face_basis(face);
      gl_.glUniformMatrix3fv(loc_basis, 1, GL_FALSE, glm::value_ptr(basis));
      gl_.glDrawArrays(GL_TRIANGLES, 0, 3);
    }
  }
  gl_.glBindFramebuffer(GL_FRAMEBUFFER, 0);
  gl_.glDeleteFramebuffers(1, &fbo);
}

void GLRendererImpl::bake_brdf_lut() {
  GLuint fbo = 0;
  gl_.glGenFramebuffers(1, &fbo);
  gl_.glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  gl_.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, brdf_lut_, 0);
  gl_.glViewport(0, 0, brdf_lut_size_, brdf_lut_size_);
  gl_.glUseProgram(prog_brdf_lut_.id());
  gl_.glDrawArrays(GL_TRIANGLES, 0, 3);
  gl_.glBindFramebuffer(GL_FRAMEBUFFER, 0);
  gl_.glDeleteFramebuffers(1, &fbo);
}

void GLRendererImpl::update_frame_uniforms() {
  if (!scene_) return;

  // Light directions live in WORLD space and are NOT rotated by the camera.
  // The previous "headlight" pattern (rotate light dirs by camera orientation
  // every frame) kept relative L·N constant as the user orbited, which made
  // the model feel flat from the default view and only revealed relief
  // through the geometric trick of side walls projecting wider at oblique
  // angles. World-fixed lighting gives the user a proper directional cue:
  // rotating the camera now actually changes how light grazes each face.
  const scene::vec3 key_world  = scene_->environment.key_direction;
  const scene::vec3 fill_world = scene_->environment.fill_direction;
  const scene::vec3 rim_world  = scene_->environment.rim_direction;

  FrameBlock fb{};
  fb.view       = scene_->camera.view();
  fb.proj       = scene_->camera.projection();
  fb.view_proj  = fb.proj * fb.view;
  fb.camera_pos = scene::vec4(scene_->camera.position(), 1.0f);
  fb.ambient    = scene::vec4(scene_->environment.ambient, 1.0f);
  fb.key_dir    = scene::vec4(glm::normalize(key_world),  0.0f);
  fb.key_color  = scene::vec4(scene_->environment.key_color, 1.0f);
  fb.fill_dir   = scene::vec4(glm::normalize(fill_world), 0.0f);
  fb.fill_color = scene::vec4(scene_->environment.fill_color, 1.0f);
  fb.rim_dir    = scene::vec4(glm::normalize(rim_world),  0.0f);
  fb.rim_color  = scene::vec4(scene_->environment.rim_color, 1.0f);

  gl_.glBindBuffer(GL_UNIFORM_BUFFER, ubo_frame_);
  gl_.glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(fb), &fb);
  gl_.glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void GLRendererImpl::draw_background(const renderer::DisplayMode& mode) {
  if (!mode.draw_background || !prog_background_.valid()) return;
  gl_.glDisable(GL_DEPTH_TEST);
  gl_.glDepthMask(GL_FALSE);
  gl_.glUseProgram(prog_background_.id());
  gl_.glUniform3fv(prog_background_.uniform(gl_, "u_top"),    1, &mode.background_top.x);
  gl_.glUniform3fv(prog_background_.uniform(gl_, "u_bottom"), 1, &mode.background_bottom.x);
  gl_.glBindVertexArray(vao_background_);
  gl_.glDrawArrays(GL_TRIANGLES, 0, 3);
  gl_.glBindVertexArray(0);
  gl_.glDepthMask(GL_TRUE);
  gl_.glEnable(GL_DEPTH_TEST);
}

void GLRendererImpl::draw_grid(const renderer::DisplayMode& mode) {
  if (!mode.show_grid || !prog_grid_.valid() || !scene_) return;
  gl_.glEnable(GL_BLEND);
  gl_.glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  gl_.glUseProgram(prog_grid_.id());

  const scene::mat4 vp     = scene_->camera.view_proj();
  const scene::mat4 inv_vp = glm::inverse(vp);
  gl_.glUniformMatrix4fv(prog_grid_.uniform(gl_, "u_view_proj"),     1, GL_FALSE, glm::value_ptr(vp));
  gl_.glUniformMatrix4fv(prog_grid_.uniform(gl_, "u_inv_view_proj"), 1, GL_FALSE, glm::value_ptr(inv_vp));

  const float radius = scene_->world_bounds.valid()
    ? std::max(scene_->world_bounds.radius(), 1.0f)
    : 1.0f;
  // Scale cell size so it is readable across model sizes.
  const float cell = std::pow(10.0f, std::floor(std::log10(radius * 0.05f)));
  gl_.glUniform1f(prog_grid_.uniform(gl_, "u_scale"), cell);
  scene::vec3 minor{0.30f, 0.32f, 0.36f};
  scene::vec3 major{0.55f, 0.58f, 0.65f};
  scene::vec3 ax_x {0.72f, 0.18f, 0.18f};
  scene::vec3 ax_z {0.18f, 0.45f, 0.72f};
  gl_.glUniform3fv(prog_grid_.uniform(gl_, "u_color_minor"),  1, &minor.x);
  gl_.glUniform3fv(prog_grid_.uniform(gl_, "u_color_major"),  1, &major.x);
  gl_.glUniform3fv(prog_grid_.uniform(gl_, "u_color_axis_x"), 1, &ax_x.x);
  gl_.glUniform3fv(prog_grid_.uniform(gl_, "u_color_axis_z"), 1, &ax_z.x);
  gl_.glUniform1f(prog_grid_.uniform(gl_, "u_fade_start"), radius * 1.5f);
  gl_.glUniform1f(prog_grid_.uniform(gl_, "u_fade_end"),   radius * 8.0f);

  gl_.glBindVertexArray(vao_grid_);
  gl_.glDrawArrays(GL_TRIANGLES, 0, 6);
  gl_.glBindVertexArray(0);
  gl_.glDisable(GL_BLEND);
}

void GLRendererImpl::draw_pivot(const renderer::DisplayMode& mode) {
  if (!mode.show_rotation_pivot || !prog_pivot_.valid() || !scene_) return;

  // Always on top: depth test off, depth write off, alpha blended.
  gl_.glEnable(GL_BLEND);
  gl_.glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  gl_.glDisable(GL_DEPTH_TEST);
  gl_.glDepthMask(GL_FALSE);

  gl_.glUseProgram(prog_pivot_.id());
  const scene::mat4 vp = scene_->camera.view_proj();
  gl_.glUniformMatrix4fv(prog_pivot_.uniform(gl_, "u_view_proj"),
                         1, GL_FALSE, glm::value_ptr(vp));
  gl_.glUniform3fv(prog_pivot_.uniform(gl_, "u_world_pos"),
                   1, &mode.rotation_pivot.x);
  gl_.glUniform1f(prog_pivot_.uniform(gl_, "u_size_px"), 30.0f);
  const scene::vec2 vp_px{static_cast<float>(viewport_w_),
                          static_cast<float>(viewport_h_)};
  gl_.glUniform2fv(prog_pivot_.uniform(gl_, "u_viewport_px"), 1, &vp_px.x);

  // Warm amber fill with a dark ring reads well against most backgrounds.
  const scene::vec4 fill{1.00f, 0.78f, 0.20f, 0.70f};
  const scene::vec4 ring{0.05f, 0.05f, 0.06f, 0.90f};
  gl_.glUniform4fv(prog_pivot_.uniform(gl_, "u_fill_color"), 1, &fill.x);
  gl_.glUniform4fv(prog_pivot_.uniform(gl_, "u_ring_color"), 1, &ring.x);

  gl_.glBindVertexArray(vao_pivot_);
  gl_.glDrawArrays(GL_TRIANGLES, 0, 6);
  gl_.glBindVertexArray(0);

  gl_.glDepthMask(GL_TRUE);
  gl_.glEnable(GL_DEPTH_TEST);
  gl_.glDisable(GL_BLEND);
}

void GLRendererImpl::draw_edges(const renderer::DisplayMode& mode) {
  if (!mode.show_edges || !prog_edges_.valid() || !scene_) return;

  gl_.glUseProgram(prog_edges_.id());
  const GLint loc_model = prog_edges_.uniform(gl_, "u_model");
  const GLint loc_color = prog_edges_.uniform(gl_, "u_color");
  const GLint loc_bias  = prog_edges_.uniform(gl_, "u_view_bias");
  // No view-space bias — depth fighting between edges and faces is
  // resolved on the surface side via glPolygonOffset (see render()),
  // which gives proper hidden-line behaviour: edges belonging to a
  // visible face pass the depth test (the face was pushed slightly
  // farther), but edges on the BACK of the part are still occluded by
  // the front-facing surface's real-ish depth.
  gl_.glUniform1f(loc_bias, 0.0f);

  // Depth test ON so back-of-part edges stay hidden. Depth write OFF so
  // the edge overlay doesn't pollute the depth buffer for any subsequent
  // pass (grid, pivot).
  gl_.glDepthMask(GL_FALSE);

  // Dark line over warm surfaces reads as ink; intensity slider in
  // DisplayMode controls overall strength.
  const float a = std::clamp(mode.edge_intensity, 0.0f, 1.0f);
  const scene::vec4 ec{0.05f, 0.06f, 0.08f, a};
  gl_.glUniform4fv(loc_color, 1, &ec.x);

  gl_.glEnable(GL_BLEND);
  gl_.glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  // Drivers ignore this in core profile but it costs nothing and helps the
  // permissive ones (Mesa, some Intel) draw 1.2 px lines.
  gl_.glLineWidth(1.2f);

  for (const auto& node : scene_->nodes) {
    if (!node.visible || !node.mesh_index) continue;
    if (*node.mesh_index >= scene_->meshes.size()) continue;
    const auto& mesh_ptr = scene_->meshes[*node.mesh_index];
    if (!mesh_ptr) continue;
    auto mit = meshes_.find(mesh_ptr.get());
    if (mit == meshes_.end()) continue;
    const auto& g = mit->second;
    if (!g.edge_vao || g.edge_index_count == 0) continue;

    gl_.glUniformMatrix4fv(loc_model, 1, GL_FALSE,
                           glm::value_ptr(node.world_matrix));
    gl_.glBindVertexArray(g.edge_vao);
    gl_.glDrawElements(GL_LINES, g.edge_index_count, GL_UNSIGNED_INT, nullptr);
  }
  gl_.glBindVertexArray(0);
  gl_.glDisable(GL_BLEND);
  gl_.glDepthMask(GL_TRUE);
}

void GLRendererImpl::render(const renderer::DisplayMode& mode) {
  if (!initialised_) initialize();
  if (!initialised_) return;

  gl_.glViewport(0, 0, viewport_w_, viewport_h_);
  gl_.glClearDepthf(1.0f);
  gl_.glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

  draw_background(mode);

  if (!scene_ || scene_->nodes.empty()) {
    draw_grid(mode);
    draw_pivot(mode);
    return;
  }

  // Update camera aspect from viewport (it may have changed via resize()).
  scene_->camera.aspect = static_cast<float>(viewport_w_) /
                          static_cast<float>(viewport_h_);
  update_frame_uniforms();

  if (!prog_pbr_.valid()) {
    CADLY_LOG_WARN("PBR program not available; skipping mesh draw.");
    draw_grid(mode);
    draw_pivot(mode);
    return;
  }

  gl_.glUseProgram(prog_pbr_.id());

  if (mode.wireframe) {
    gl_.glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    gl_.glDisable(GL_CULL_FACE);
  }

  // When the edge overlay is on, push the surface fragments slightly away
  // from the camera in depth space. The edges keep their true depth, so:
  //   - edges on a visible face pass the depth test against the pushed-back
  //     surface and draw on top of it (no Z-fighting),
  //   - edges on the BACK of the part still fail the depth test against
  //     the front-facing surface — i.e. hidden lines stay hidden.
  // This is the classic CAD "shaded with edges" recipe.
  if (mode.show_edges) {
    gl_.glEnable(GL_POLYGON_OFFSET_FILL);
    gl_.glPolygonOffset(1.0f, 1.0f);
  }

  // Bind IBL targets to fixed texture units. The samplers don't change
  // per-draw, so set once outside the mesh loop.
  gl_.glActiveTexture(GL_TEXTURE0);
  gl_.glBindTexture(GL_TEXTURE_CUBE_MAP, irradiance_cube_);
  gl_.glUniform1i(prog_pbr_.uniform(gl_, "u_irradiance_cube"), 0);
  gl_.glActiveTexture(GL_TEXTURE1);
  gl_.glBindTexture(GL_TEXTURE_CUBE_MAP, prefilter_cube_);
  gl_.glUniform1i(prog_pbr_.uniform(gl_, "u_prefilter_cube"), 1);
  gl_.glActiveTexture(GL_TEXTURE2);
  gl_.glBindTexture(GL_TEXTURE_2D, brdf_lut_);
  gl_.glUniform1i(prog_pbr_.uniform(gl_, "u_brdf_lut"), 2);
  gl_.glUniform1f(prog_pbr_.uniform(gl_, "u_prefilter_max_lod"),
                  static_cast<float>(prefilter_mip_count_ - 1));

  const GLint loc_model     = prog_pbr_.uniform(gl_, "u_model");
  const GLint loc_normal_m  = prog_pbr_.uniform(gl_, "u_normal_matrix");
  const GLint loc_base      = prog_pbr_.uniform(gl_, "u_base_color");
  const GLint loc_metal     = prog_pbr_.uniform(gl_, "u_metallic");
  const GLint loc_rough     = prog_pbr_.uniform(gl_, "u_roughness");
  const GLint loc_refl      = prog_pbr_.uniform(gl_, "u_reflectance");
  const GLint loc_emi_col   = prog_pbr_.uniform(gl_, "u_emissive_color");
  const GLint loc_emi       = prog_pbr_.uniform(gl_, "u_emissive");

  for (const auto& node : scene_->nodes) {
    if (!node.visible || !node.mesh_index) continue;
    if (*node.mesh_index >= scene_->meshes.size()) continue;
    const auto& mesh_ptr = scene_->meshes[*node.mesh_index];
    if (!mesh_ptr) continue;

    ensure_mesh_upload(*mesh_ptr, mesh_ptr);
    auto mit = meshes_.find(mesh_ptr.get());
    if (mit == meshes_.end()) continue;
    const auto& g = mit->second;

    const scene::mat4 model = node.world_matrix;
    const scene::mat3 normal_matrix = glm::transpose(glm::inverse(scene::mat3(model)));
    gl_.glUniformMatrix4fv(loc_model,    1, GL_FALSE, glm::value_ptr(model));
    gl_.glUniformMatrix3fv(loc_normal_m, 1, GL_FALSE, glm::value_ptr(normal_matrix));

    gl_.glBindVertexArray(g.vao);
    for (const auto& sub : mesh_ptr->submeshes) {
      const std::uint32_t mat_idx = node.material_override
        ? *node.material_override : sub.material_index;
      const scene::Material* mat = mat_idx < scene_->materials.size()
        ? &scene_->materials[mat_idx] : nullptr;
      scene::Material fallback;
      const scene::Material& m = mat ? *mat : fallback;

      gl_.glUniform4fv(loc_base,    1, &m.base_color.x);
      gl_.glUniform1f (loc_metal,   m.metallic);
      gl_.glUniform1f (loc_rough,   m.roughness);
      gl_.glUniform1f (loc_refl,    m.reflectance);
      gl_.glUniform3fv(loc_emi_col, 1, &m.emissive_color.x);
      gl_.glUniform1f (loc_emi,     m.emissive);

      if (m.double_sided) {
        gl_.glDisable(GL_CULL_FACE);
      } else {
        gl_.glEnable(GL_CULL_FACE);
      }

      gl_.glDrawElements(GL_TRIANGLES,
                         static_cast<GLsizei>(sub.index_count),
                         GL_UNSIGNED_INT,
                         reinterpret_cast<void*>(
                           static_cast<std::uintptr_t>(sub.index_offset) * sizeof(std::uint32_t)));
    }
    gl_.glBindVertexArray(0);
  }

  if (mode.wireframe) {
    gl_.glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    gl_.glEnable(GL_CULL_FACE);
  }
  if (mode.show_edges) {
    gl_.glDisable(GL_POLYGON_OFFSET_FILL);
  }

  draw_edges(mode);
  draw_grid(mode);
  draw_pivot(mode);
}

void GLRendererImpl::shutdown() {
  if (!initialised_) return;
  for (auto& [_, g] : meshes_) {
    if (g.vao) gl_.glDeleteVertexArrays(1, &g.vao);
    if (g.vbo) gl_.glDeleteBuffers(1, &g.vbo);
    if (g.ibo) gl_.glDeleteBuffers(1, &g.ibo);
    if (g.edge_vao) gl_.glDeleteVertexArrays(1, &g.edge_vao);
    if (g.edge_vbo) gl_.glDeleteBuffers(1, &g.edge_vbo);
    if (g.edge_ibo) gl_.glDeleteBuffers(1, &g.edge_ibo);
  }
  meshes_.clear();
  if (ubo_frame_)      gl_.glDeleteBuffers(1, &ubo_frame_);
  if (vao_background_) gl_.glDeleteVertexArrays(1, &vao_background_);
  if (vao_grid_)       gl_.glDeleteVertexArrays(1, &vao_grid_);
  if (vbo_grid_)       gl_.glDeleteBuffers(1, &vbo_grid_);
  if (vao_pivot_)      gl_.glDeleteVertexArrays(1, &vao_pivot_);
  if (vao_quad_)       gl_.glDeleteVertexArrays(1, &vao_quad_);
  if (env_cube_)        gl_.glDeleteTextures(1, &env_cube_);
  if (irradiance_cube_) gl_.glDeleteTextures(1, &irradiance_cube_);
  if (prefilter_cube_)  gl_.glDeleteTextures(1, &prefilter_cube_);
  if (brdf_lut_)        gl_.glDeleteTextures(1, &brdf_lut_);
  ubo_frame_ = vao_background_ = vao_grid_ = vbo_grid_ = vao_pivot_ = vao_quad_ = 0;
  env_cube_ = irradiance_cube_ = prefilter_cube_ = brdf_lut_ = 0;
  prog_pbr_.destroy(gl_);
  prog_background_.destroy(gl_);
  prog_grid_.destroy(gl_);
  prog_pivot_.destroy(gl_);
  prog_edges_.destroy(gl_);
  prog_env_capture_.destroy(gl_);
  prog_irradiance_.destroy(gl_);
  prog_prefilter_.destroy(gl_);
  prog_brdf_lut_.destroy(gl_);
  initialised_ = false;
}

} // namespace

std::unique_ptr<renderer::IRenderer> make_gl_renderer() {
  return std::make_unique<GLRendererImpl>();
}

} // namespace cadly::renderer_gl

#include "cadly/renderer_gl/GLRenderer.h"

#include "GLFunctions.h"
#include "GLShader.h"

#include "cadly/platform/Log.h"
#include "cadly/scene/Math.h"
#include "cadly/scene/Mesh.h"
#include "cadly/scene/Node.h"
#include "cadly/scene/Scene.h"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cadly::renderer_gl {

namespace {

using detail::GLFunctions;
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

// One GL buffer set per BRep edge LOD tier (vao + vbo + ibo + index count).
// Allocated lazily as part of MeshGpu when the source mesh carries edges.
struct EdgeLodGpu {
  GLuint  vao        {0};
  GLuint  vbo        {0};
  GLuint  ibo        {0};
  GLsizei index_count{0};
  float   linear_deflection{0.0f}; // copied from scene::Mesh::EdgeLod
};

// One per scene::Mesh. Cached so re-uploading happens only when the source
// pointer changes.
struct MeshGpu {
  GLuint vao{0};
  GLuint vbo{0};
  GLuint ibo{0};
  GLsizei index_count{0};
  // Mesh-coupled BRep edge buffer. Reuses `vbo` (the surface vertex
  // buffer) so edge vertices and face vertices share identical depth
  // before polygon offset is applied — the "shaded with edges" overlay
  // can rely on offset alone to keep edges in front of their faces.
  // Allocated only when the source mesh carries `edge_strip_indices`.
  GLuint strip_vao  {0};
  GLuint strip_ibo  {0};
  GLsizei strip_index_count{0};
  // BRep edge LOD ladder, sampled analytically. Kept around for the
  // "edges without surfaces" view; not used by the default shaded-with-
  // edges path because mesh-coupled indices are guaranteed consistent
  // with the face triangulation, while the analytical curve is not.
  std::vector<EdgeLodGpu> edge_lods;
  // Strong reference to the source CPU mesh. Two reasons:
  //   1. Anchors the raw `Mesh*` cache key — as long as we hold the
  //      shared_ptr, the address can't be reused by an unrelated mesh,
  //      so the ABA hazard on map lookup is gone.
  //   2. Mirrors the GPU buffer lifetime: when this entry is evicted
  //      (in `attach_scene` or `shutdown`), the CPU geometry is allowed
  //      to drop too, instead of being kept alive forever as it was
  //      before the eviction pass existed.
  std::shared_ptr<scene::Mesh> source;
};

struct OverlayVertex {
  float x;
  float y;
  float r;
  float g;
  float b;
  float a;
};

std::string trim_number(std::ostringstream& ss) {
  std::string out = ss.str();
  if (out.find('.') == std::string::npos) return out;
  while (!out.empty() && out.back() == '0') out.pop_back();
  if (!out.empty() && out.back() == '.') out.pop_back();
  return out.empty() ? "0" : out;
}

std::string format_length(double meters) {
  struct Unit {
    double scale;
    const char* label;
  };
  const double abs_m = std::abs(meters);
  Unit unit{1.0, "m"};
  if (abs_m < 1e-6) {
    unit = {1e9, "nm"};
  } else if (abs_m < 1e-3) {
    unit = {1e6, "um"};
  } else if (abs_m < 0.1) {
    unit = {1e3, "mm"};
  } else if (abs_m < 1.0) {
    unit = {1e2, "cm"};
  } else if (abs_m >= 1000.0) {
    unit = {1e-3, "km"};
  }

  const double value = meters * unit.scale;
  std::ostringstream ss;
  if (std::abs(value) >= 100.0) {
    ss << std::fixed << std::setprecision(0) << value;
  } else if (std::abs(value) >= 10.0) {
    ss << std::fixed << std::setprecision(1) << value;
  } else {
    ss << std::fixed << std::setprecision(2) << value;
  }
  return trim_number(ss) + " " + unit.label;
}

double nice_length(double target_meters) {
  if (!std::isfinite(target_meters) || target_meters <= 0.0) return 0.0;
  const double exponent = std::floor(std::log10(target_meters));
  const double base = std::pow(10.0, exponent);
  const double normalized = target_meters / base;
  double step = 10.0;
  if (normalized <= 1.0) {
    step = 1.0;
  } else if (normalized <= 2.0) {
    step = 2.0;
  } else if (normalized <= 5.0) {
    step = 5.0;
  }
  return step * base;
}

void emit_rect(std::vector<OverlayVertex>& out,
               float x,
               float y,
               float w,
               float h,
               const scene::vec4& color) {
  const OverlayVertex a{x,     y,     color.r, color.g, color.b, color.a};
  const OverlayVertex b{x + w, y,     color.r, color.g, color.b, color.a};
  const OverlayVertex c{x + w, y + h, color.r, color.g, color.b, color.a};
  const OverlayVertex d{x,     y + h, color.r, color.g, color.b, color.a};
  out.insert(out.end(), {a, b, c, a, c, d});
}

float glyph_advance(char c, float scale) {
  if (c == ' ') return 2.8f * scale;
  if (c == '.') return 2.2f * scale;
  if (c == 'm' || c == 'w') return 6.4f * scale;
  return 5.2f * scale;
}

float text_width_px(const std::string& text, float scale) {
  float width = 0.0f;
  for (char c : text) width += glyph_advance(c, scale);
  return std::max(0.0f, width - scale * 0.8f);
}

void emit_h_stroke(std::vector<OverlayVertex>& out,
                   float x,
                   float y,
                   float w,
                   float t,
                   const scene::vec4& color) {
  emit_rect(out, x, y - t * 0.5f, w, t, color);
}

void emit_v_stroke(std::vector<OverlayVertex>& out,
                   float x,
                   float y,
                   float h,
                   float t,
                   const scene::vec4& color) {
  emit_rect(out, x - t * 0.5f, y, t, h, color);
}

void emit_dot(std::vector<OverlayVertex>& out,
              float x,
              float y,
              float scale,
              const scene::vec4& color) {
  const float s = std::max(1.4f, scale * 0.58f);
  emit_rect(out, x, y, s, s, color);
}

void emit_digit(std::vector<OverlayVertex>& out,
                int digit,
                float x,
                float y,
                float scale,
                const scene::vec4& color) {
  static constexpr std::array<std::uint8_t, 10> masks{
    0b1111110, // 0: top, upper-right, lower-right, bottom, lower-left, upper-left
    0b0110000,
    0b1101101,
    0b1111001,
    0b0110011,
    0b1011011,
    0b1011111,
    0b1110000,
    0b1111111,
    0b1111011,
  };
  const float w = 4.4f * scale;
  const float h = 6.6f * scale;
  const float t = std::max(1.2f, scale * 0.34f);
  const std::uint8_t m = masks[static_cast<std::size_t>(digit)];
  if (m & 0b1000000) emit_h_stroke(out, x + t, y + h, w - 2.0f * t, t, color);
  if (m & 0b0100000) emit_v_stroke(out, x + w, y + h * 0.52f, h * 0.45f, t, color);
  if (m & 0b0010000) emit_v_stroke(out, x + w, y + t, h * 0.43f, t, color);
  if (m & 0b0001000) emit_h_stroke(out, x + t, y, w - 2.0f * t, t, color);
  if (m & 0b0000100) emit_v_stroke(out, x, y + t, h * 0.43f, t, color);
  if (m & 0b0000010) emit_v_stroke(out, x, y + h * 0.52f, h * 0.45f, t, color);
  if (m & 0b0000001) emit_h_stroke(out, x + t, y + h * 0.5f, w - 2.0f * t, t, color);
}

void emit_unit_char(std::vector<OverlayVertex>& out,
                    char c,
                    float x,
                    float y,
                    float scale,
                    const scene::vec4& color) {
  const float w = (c == 'm') ? 5.8f * scale : 4.2f * scale;
  const float h = 4.4f * scale;
  const float t = std::max(1.0f, scale * 0.30f);
  const float mid = y + h * 0.5f;
  switch (c) {
    case 'c':
      emit_h_stroke(out, x + t, y + h, w - t, t, color);
      emit_h_stroke(out, x + t, y, w - t, t, color);
      emit_v_stroke(out, x, y + t, h - 2.0f * t, t, color);
      break;
    case 'k':
      emit_v_stroke(out, x, y, h, t, color);
      emit_rect(out, x + w * 0.44f, mid - t * 0.5f, w * 0.42f, t, color);
      emit_rect(out, x + w * 0.55f, mid, t, h * 0.5f, color);
      emit_rect(out, x + w * 0.55f, y, t, h * 0.5f, color);
      break;
    case 'm':
      emit_v_stroke(out, x, y, h, t, color);
      emit_v_stroke(out, x + w * 0.45f, y, h * 0.78f, t, color);
      emit_v_stroke(out, x + w, y, h * 0.78f, t, color);
      emit_h_stroke(out, x + t, y + h, w - t, t, color);
      break;
    case 'n':
      emit_v_stroke(out, x, y, h, t, color);
      emit_v_stroke(out, x + w, y, h * 0.78f, t, color);
      emit_h_stroke(out, x + t, y + h, w - t, t, color);
      break;
    case 'u':
      emit_v_stroke(out, x, y + t, h - t, t, color);
      emit_v_stroke(out, x + w, y + t, h - t, t, color);
      emit_h_stroke(out, x + t, y, w - t, t, color);
      break;
    default:
      break;
  }
}

void emit_text(std::vector<OverlayVertex>& out,
               const std::string& text,
               float x,
               float y,
               float scale,
               const scene::vec4& color) {
  float cursor = x;
  for (char c : text) {
    if (c >= '0' && c <= '9') {
      emit_digit(out, c - '0', cursor, y, scale, color);
    } else if (c == '.') {
      emit_dot(out, cursor, y, scale, color);
    } else if (c != ' ') {
      emit_unit_char(out, c, cursor, y, scale, color);
    }
    cursor += glyph_advance(c, scale);
  }
}

// Implementation -----------------------------------------------------------
class GLRendererImpl final : public renderer::IRenderer {
public:
  explicit GLRendererImpl(GLLoadProc load_proc)
      : load_proc_(std::move(load_proc)) {}
  ~GLRendererImpl() override { /* shutdown() handles cleanup explicitly */ }

  void initialize() override;
  void resize(int width_px, int height_px) override;
  void attach_scene(std::shared_ptr<scene::Scene> scene) override;
  void render(const renderer::DisplayMode& mode) override;
  void shutdown() override;

private:
  bool build_programs();
  void ensure_mesh_upload(const scene::Mesh& mesh,
                          const std::shared_ptr<scene::Mesh>& mesh_ptr);
  void free_mesh_gpu(MeshGpu& g);
  void update_frame_uniforms();
  void draw_background(const renderer::DisplayMode& mode);
  void draw_pivot(const renderer::DisplayMode& mode);
  void draw_scale_bar();
  void draw_edges(const renderer::DisplayMode& mode);
  void draw_triangle_mesh(const renderer::DisplayMode& mode);

  // MSAA offscreen target. The renderer owns its own multisample colour +
  // depth renderbuffers so anti-aliasing quality is decoupled from the host
  // surface (Qt asks for a single-sample default framebuffer; we resolve
  // into it at the end of the frame). The (re)allocation is lazy: it
  // happens the first time a frame requests samples > 1 and again whenever
  // the viewport size or requested sample count changes.
  int  clamp_msaa_samples(int requested) const;
  void ensure_msaa_target(int width_px, int height_px, int samples);
  void release_msaa_target();

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

  GLLoadProc load_proc_;
  GLFunctions gl_;
  bool   initialised_{false};
  GLint  viewport_w_{1};
  GLint  viewport_h_{1};

  // Programs.
  GLProgram prog_pbr_;
  GLProgram prog_background_;
  GLProgram prog_pivot_;
  GLProgram prog_overlay_;
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

  // Rotation-pivot indicator (empty VAO; uses gl_VertexID).
  GLuint   vao_pivot_{0};

  // Renderer-owned 2D overlay geometry. Used for the scale bar and stroked
  // text so Qt remains only the GUI/input/context host.
  GLuint   vao_overlay_{0};
  GLuint   vbo_overlay_{0};
  std::vector<OverlayVertex> overlay_vertices_;

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

  // Scene + GPU mesh cache. Keyed by `const scene::Mesh*` purely to avoid the
  // const_cast that the previous non-const key forced. ABA address-reuse
  // hazards are blocked by `MeshGpu::source` (the strong ref keeps the
  // address pinned), and `attach_scene` evicts entries that the new scene
  // no longer references — without that pass, the source shared_ptr would
  // turn into a slow GPU-side leak across re-imports.
  std::shared_ptr<scene::Scene> scene_;
  std::unordered_map<const scene::Mesh*, MeshGpu> meshes_;

  // MSAA target. fbo_msaa_ == 0 means "not allocated" (either MSAA is off,
  // or the very first frame hasn't run yet). msaa_samples_current_ records
  // the sample count actually allocated, which may differ from the request
  // after GL_MAX_SAMPLES clamping. fbo_w_/fbo_h_ track the renderbuffer
  // storage size so resize requests trigger a re-alloc.
  GLuint fbo_msaa_              {0};
  GLuint rb_color_msaa_         {0};
  GLuint rb_depth_msaa_         {0};
  int    msaa_samples_current_  {0};
  int    fbo_w_                 {0};
  int    fbo_h_                 {0};
  int    max_samples_           {0};
};

void GLRendererImpl::initialize() {
  if (initialised_) return;
  if (!gl_.load(load_proc_)) {
    CADLY_LOG_ERROR("Failed to load OpenGL 4.1 core functions. "
                    "Is the context current and a compatible version?");
    return;
  }

  // Cap of MSAA sample counts we will accept later. Every GL 4.1 core
  // implementation supports at least 4; modern desktop GPUs typically
  // report 8–32. Query once so render() can clamp `mode.msaa_samples`
  // without going around the GL every frame.
  GLint max_samples = 0;
  gl_.glGetIntegerv(GL_MAX_SAMPLES, &max_samples);
  max_samples_ = std::max(0, static_cast<int>(max_samples));

  gl_.glEnable(GL_DEPTH_TEST);
  gl_.glDepthFunc(GL_LEQUAL);
  gl_.glEnable(GL_CULL_FACE);
  gl_.glCullFace(GL_BACK);
  gl_.glFrontFace(GL_CCW);
  gl_.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  // Always clear the colour buffer to opaque black (alpha = 1). The OpenGL
  // default clear colour is (0,0,0,0) — alpha = 0 — which gets presented to
  // the OS compositor as a fully-transparent window region anywhere the
  // background pass doesn't run or fails to cover. Setting it explicitly
  // here (persistent state, set once) means the wireframe/edges/grid/pivot
  // passes can rely on dst alpha starting at 1 even if mode.draw_background
  // is ever toggled off, and the alpha-preserving blend funcs below keep
  // it there.
  gl_.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

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

  // Pivot indicator: empty VAO, geometry emitted from gl_VertexID.
  gl_.glGenVertexArrays(1, &vao_pivot_);

  gl_.glGenVertexArrays(1, &vao_overlay_);
  gl_.glGenBuffers(1, &vbo_overlay_);
  gl_.glBindVertexArray(vao_overlay_);
  gl_.glBindBuffer(GL_ARRAY_BUFFER, vbo_overlay_);
  gl_.glEnableVertexAttribArray(0);
  gl_.glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                            sizeof(OverlayVertex),
                            reinterpret_cast<void*>(offsetof(OverlayVertex, x)));
  gl_.glEnableVertexAttribArray(1);
  gl_.glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE,
                            sizeof(OverlayVertex),
                            reinterpret_cast<void*>(offsetof(OverlayVertex, r)));
  gl_.glBindVertexArray(0);
  gl_.glBindBuffer(GL_ARRAY_BUFFER, 0);

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
  build(prog_pivot_,       "pivot.vert",       "pivot.frag",       "pivot");
  build(prog_edges_,       "edges.vert",       "edges.frag",       "edges");
  build(prog_env_capture_, "env_capture.vert", "env_capture.frag", "env_capture");
  build(prog_irradiance_,  "irradiance.vert",  "irradiance.frag",  "irradiance");
  build(prog_prefilter_,   "prefilter.vert",   "prefilter.frag",   "prefilter");
  build(prog_brdf_lut_,    "brdf_lut.vert",    "brdf_lut.frag",    "brdf_lut");

  const char* overlay_vs = R"glsl(
#version 410 core
layout(location = 0) in vec2 a_pos_px;
layout(location = 1) in vec4 a_color;
uniform vec2 u_viewport_px;
out vec4 v_color;
void main() {
  vec2 ndc = vec2((a_pos_px.x / u_viewport_px.x) * 2.0 - 1.0,
                  (a_pos_px.y / u_viewport_px.y) * 2.0 - 1.0);
  gl_Position = vec4(ndc, 0.0, 1.0);
  v_color = a_color;
}
)glsl";
  const char* overlay_fs = R"glsl(
#version 410 core
in vec4 v_color;
out vec4 frag_color;
void main() {
  frag_color = v_color;
}
)glsl";
  if (!prog_overlay_.build(gl_, overlay_vs, overlay_fs, "overlay")) {
    all_ok = false;
  }
  return all_ok;
}

void GLRendererImpl::resize(int width_px, int height_px) {
  viewport_w_ = std::max(1, width_px);
  viewport_h_ = std::max(1, height_px);
}

void GLRendererImpl::attach_scene(std::shared_ptr<scene::Scene> scene) {
  // Evict GPU buffers for meshes that the new scene no longer references.
  // Without this pass the `MeshGpu::source` strong refs would pin every CPU
  // mesh forever, and the GPU buffers would never be freed across re-imports.
  if (initialised_) {
    std::unordered_set<const scene::Mesh*> still_alive;
    if (scene) {
      still_alive.reserve(scene->meshes.size());
      for (const auto& m : scene->meshes) {
        if (m) still_alive.insert(m.get());
      }
    }
    for (auto it = meshes_.begin(); it != meshes_.end(); ) {
      if (still_alive.find(it->first) == still_alive.end()) {
        free_mesh_gpu(it->second);
        it = meshes_.erase(it);
      } else {
        ++it;
      }
    }
  }
  scene_ = std::move(scene);
}

void GLRendererImpl::free_mesh_gpu(MeshGpu& g) {
  if (g.vao) gl_.glDeleteVertexArrays(1, &g.vao);
  if (g.vbo) gl_.glDeleteBuffers(1, &g.vbo);
  if (g.ibo) gl_.glDeleteBuffers(1, &g.ibo);
  // strip_vao reuses g.vbo, so only its own VAO + IBO are owned here.
  if (g.strip_vao) gl_.glDeleteVertexArrays(1, &g.strip_vao);
  if (g.strip_ibo) gl_.glDeleteBuffers(1, &g.strip_ibo);
  for (auto& lod : g.edge_lods) {
    if (lod.vao) gl_.glDeleteVertexArrays(1, &lod.vao);
    if (lod.vbo) gl_.glDeleteBuffers(1, &lod.vbo);
    if (lod.ibo) gl_.glDeleteBuffers(1, &lod.ibo);
  }
  g = MeshGpu{};
}

void GLRendererImpl::ensure_mesh_upload(const scene::Mesh& mesh,
                                        const std::shared_ptr<scene::Mesh>& mesh_ptr) {
  auto it = meshes_.find(&mesh);
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

  // Mesh-coupled BRep edge strip. Indices point into the surface VBO we
  // just uploaded, so we share the VBO (no extra vertex storage), bind a
  // fresh VAO with the position attribute set up the same way, and attach
  // a new IBO that holds one GL_LINE_STRIP per edge separated by the
  // 0xFFFFFFFF restart sentinel (see OcctShapeToMesh.cpp). The edge
  // shader reads attribute 0 only; normal/color attributes from the
  // surface layout are ignored.
  if (!mesh.edge_strip_indices.empty()) {
    gl_.glGenVertexArrays(1, &g.strip_vao);
    gl_.glGenBuffers(1, &g.strip_ibo);
    gl_.glBindVertexArray(g.strip_vao);
    gl_.glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
    gl_.glEnableVertexAttribArray(0);
    gl_.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                              sizeof(scene::Vertex),
                              reinterpret_cast<void*>(offsetof(scene::Vertex, position)));
    gl_.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.strip_ibo);
    gl_.glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(mesh.edge_strip_indices.size() *
                                             sizeof(std::uint32_t)),
                     mesh.edge_strip_indices.data(),
                     GL_STATIC_DRAW);
    g.strip_index_count =
      static_cast<GLsizei>(mesh.edge_strip_indices.size());
    gl_.glBindVertexArray(0);
  }

  // Edge polylines, if the importer captured any. The mesh carries a LOD
  // ladder (coarse → fine); each tier becomes its own VAO/VBO/IBO so the
  // draw pass can switch tiers with a single glBindVertexArray. Vertex
  // layout is a flat packed array of vec3 positions; indices form one
  // GL_LINE_STRIP per edge separated by the 0xFFFFFFFF primitive-restart
  // sentinel (draw_edges binds the sentinel via glPrimitiveRestartIndex).
  g.edge_lods.reserve(mesh.edge_lods.size());
  for (const auto& lod : mesh.edge_lods) {
    if (lod.vertices.empty() || lod.indices.empty()) continue;
    EdgeLodGpu gl_lod{};
    gl_.glGenVertexArrays(1, &gl_lod.vao);
    gl_.glGenBuffers(1, &gl_lod.vbo);
    gl_.glGenBuffers(1, &gl_lod.ibo);
    gl_.glBindVertexArray(gl_lod.vao);
    gl_.glBindBuffer(GL_ARRAY_BUFFER, gl_lod.vbo);
    gl_.glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(lod.vertices.size() *
                                             sizeof(scene::vec3)),
                     lod.vertices.data(),
                     GL_STATIC_DRAW);
    gl_.glEnableVertexAttribArray(0);
    gl_.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                              sizeof(scene::vec3), nullptr);
    gl_.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gl_lod.ibo);
    gl_.glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(lod.indices.size() *
                                             sizeof(std::uint32_t)),
                     lod.indices.data(),
                     GL_STATIC_DRAW);
    gl_lod.index_count       = static_cast<GLsizei>(lod.indices.size());
    gl_lod.linear_deflection = lod.linear_deflection;
    g.edge_lods.push_back(gl_lod);
  }
  gl_.glBindVertexArray(0);

  meshes_.emplace(&mesh, std::move(g));
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

void GLRendererImpl::draw_pivot(const renderer::DisplayMode& mode) {
  if (!mode.show_rotation_pivot || !prog_pivot_.valid() || !scene_) return;

  // Always on top: depth test off, depth write off, alpha blended.
  // Alpha-preserving blend (dst.alpha unchanged) so the pivot marker
  // doesn't leave a translucent halo against the compositor.
  gl_.glEnable(GL_BLEND);
  gl_.glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                          GL_ZERO,      GL_ONE);
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

void GLRendererImpl::draw_scale_bar() {
  if (!scene_ || !prog_overlay_.valid() || vao_overlay_ == 0 || vbo_overlay_ == 0) return;
  if (viewport_w_ < 220 || viewport_h_ < 120) return;

  const auto& cam = scene_->camera;
  const float screen_height_world =
    2.0f * std::max(cam.distance, 1e-6f) * std::tan(0.5f * cam.fov_y);
  const double world_per_pixel =
    static_cast<double>(screen_height_world) /
    std::max(static_cast<double>(viewport_h_), 1.0);
  const double meters_per_pixel =
    world_per_pixel * std::max(static_cast<double>(scene_->unit_to_meters), 1e-12);
  if (!std::isfinite(meters_per_pixel) || meters_per_pixel <= 0.0) return;

  const float target_px =
    std::clamp(static_cast<float>(viewport_w_) * 0.18f, 90.0f, 150.0f);
  const double bar_meters = nice_length(static_cast<double>(target_px) * meters_per_pixel);
  if (bar_meters <= 0.0) return;
  const float bar_px = static_cast<float>(bar_meters / meters_per_pixel);
  if (!std::isfinite(bar_px) || bar_px < 40.0f) return;

  const std::string label = format_length(bar_meters);
  const float text_scale = viewport_w_ >= 520 ? 2.55f : 2.1f;
  const float text_w = text_width_px(label, text_scale);

  const float inset = 22.0f;
  const float bar_x = inset;
  const float bar_y = 24.0f;
  const float bar_h = 2.0f;
  const float tick_h = 10.0f;
  const float text_h = 6.6f * text_scale;
  const float text_x = bar_x + (bar_px - text_w) * 0.5f;
  const float text_y = bar_y + tick_h + 6.0f;
  const float plate_pad_x = 9.0f;
  const float plate_pad_y = 7.0f;
  const float plate_x = std::min(bar_x, text_x) - plate_pad_x;
  const float plate_y = bar_y - plate_pad_y;
  const float plate_w = std::max(bar_px, text_w) + plate_pad_x * 2.0f;
  const float plate_h = (text_y + text_h - bar_y) + plate_pad_y * 2.0f;

  overlay_vertices_.clear();
  overlay_vertices_.reserve(1024);

  const scene::vec4 plate{0.03f, 0.035f, 0.04f, 0.30f};
  const scene::vec4 shadow{0.0f, 0.0f, 0.0f, 0.55f};
  const scene::vec4 ink{0.88f, 0.91f, 0.94f, 0.88f};
  const scene::vec4 soft_ink{0.88f, 0.91f, 0.94f, 0.42f};

  emit_rect(overlay_vertices_, plate_x, plate_y, plate_w, plate_h, plate);

  emit_rect(overlay_vertices_, bar_x + 1.0f, bar_y - 1.0f, bar_px, bar_h, shadow);
  emit_rect(overlay_vertices_, bar_x, bar_y, bar_px, bar_h, ink);
  emit_rect(overlay_vertices_, bar_x, bar_y - tick_h * 0.5f, 2.0f, tick_h, ink);
  emit_rect(overlay_vertices_, bar_x + bar_px - 2.0f, bar_y - tick_h * 0.5f, 2.0f, tick_h, ink);
  emit_rect(overlay_vertices_, bar_x + bar_px * 0.5f - 0.5f, bar_y - tick_h * 0.32f,
            1.0f, tick_h * 0.64f, soft_ink);

  emit_text(overlay_vertices_, label, text_x + 1.0f, text_y - 1.0f, text_scale, shadow);
  emit_text(overlay_vertices_, label, text_x, text_y, text_scale, ink);

  gl_.glEnable(GL_BLEND);
  gl_.glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                          GL_ZERO,      GL_ONE);
  gl_.glDisable(GL_DEPTH_TEST);
  gl_.glDepthMask(GL_FALSE);
  gl_.glDisable(GL_CULL_FACE);
  gl_.glDisable(GL_PRIMITIVE_RESTART);
  gl_.glDisable(GL_POLYGON_OFFSET_FILL);
  gl_.glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

  gl_.glUseProgram(prog_overlay_.id());
  const scene::vec2 vp_px{static_cast<float>(viewport_w_),
                          static_cast<float>(viewport_h_)};
  gl_.glUniform2fv(prog_overlay_.uniform(gl_, "u_viewport_px"), 1, &vp_px.x);
  gl_.glBindVertexArray(vao_overlay_);
  gl_.glBindBuffer(GL_ARRAY_BUFFER, vbo_overlay_);
  gl_.glBufferData(GL_ARRAY_BUFFER,
                   static_cast<GLsizeiptr>(overlay_vertices_.size() *
                                           sizeof(OverlayVertex)),
                   overlay_vertices_.data(),
                   GL_DYNAMIC_DRAW);
  gl_.glDrawArrays(GL_TRIANGLES, 0,
                   static_cast<GLsizei>(overlay_vertices_.size()));
  gl_.glBindBuffer(GL_ARRAY_BUFFER, 0);
  gl_.glBindVertexArray(0);

  gl_.glDepthMask(GL_TRUE);
  gl_.glEnable(GL_DEPTH_TEST);
  gl_.glEnable(GL_CULL_FACE);
  gl_.glDisable(GL_BLEND);
}

void GLRendererImpl::draw_triangle_mesh(const renderer::DisplayMode& mode) {
  // Debug overlay: re-draw the surface triangles in GL_LINE polygon mode
  // using the edges shader. Reuses the surface VAO + IBO, so the triangle
  // edges sit exactly on the same depth values as the filled surface —
  // glPolygonOffset on the surface (enabled in render() when any line
  // overlay is on) keeps these lines in front of the fill without
  // Z-fighting, and the existing depth test correctly hides triangles on
  // the back of the part.
  if (!mode.show_triangle_mesh || !prog_edges_.valid() || !scene_) return;

  gl_.glUseProgram(prog_edges_.id());
  const GLint loc_model = prog_edges_.uniform(gl_, "u_model");
  const GLint loc_color = prog_edges_.uniform(gl_, "u_color");
  const GLint loc_bias  = prog_edges_.uniform(gl_, "u_view_bias");
  gl_.glUniform1f(loc_bias, 0.0f);
  // Cooler / lighter than the BRep edge ink so the two overlays are
  // distinguishable when both are on. Mid-alpha to soften the inevitable
  // double-draw where a triangle edge coincides with a BRep edge.
  const scene::vec4 tc{0.20f, 0.45f, 0.60f, 0.55f};
  gl_.glUniform4fv(loc_color, 1, &tc.x);

  gl_.glEnable(GL_BLEND);
  // Alpha-preserving blend: src.rgb blends in normally with SRC_ALPHA, but
  // dst.alpha is left untouched (factor ZERO on src, ONE on dst). Without
  // this, every blended pixel would decay dst.alpha toward zero and the OS
  // compositor would punch holes wherever a faded line crosses the screen.
  gl_.glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                          GL_ZERO,      GL_ONE);
  gl_.glDepthMask(GL_FALSE);
  gl_.glLineWidth(1.0f);
  gl_.glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
  // Triangles are not "facing" in line mode — drop culling for the pass
  // so back-of-part triangles also draw, then let the depth test hide
  // those that the front-facing surface already covers.
  gl_.glDisable(GL_CULL_FACE);

  for (const auto& node : scene_->nodes) {
    if (!node.visible || !node.mesh_index) continue;
    if (*node.mesh_index >= scene_->meshes.size()) continue;
    const auto& mesh_ptr = scene_->meshes[*node.mesh_index];
    if (!mesh_ptr) continue;
    auto mit = meshes_.find(mesh_ptr.get());
    if (mit == meshes_.end()) continue;
    const auto& g = mit->second;
    if (g.vao == 0 || g.index_count == 0) continue;

    gl_.glUniformMatrix4fv(loc_model, 1, GL_FALSE,
                           glm::value_ptr(node.world_matrix));
    gl_.glBindVertexArray(g.vao);
    gl_.glDrawElements(GL_TRIANGLES, g.index_count, GL_UNSIGNED_INT, nullptr);
  }

  gl_.glBindVertexArray(0);
  gl_.glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  gl_.glEnable(GL_CULL_FACE);
  gl_.glDisable(GL_BLEND);
  gl_.glDepthMask(GL_TRUE);
}

void GLRendererImpl::draw_edges(const renderer::DisplayMode& mode) {
  // Two display modes feed this pass:
  //   - mode.show_edges : overlay BRep edges on the shaded surface using
  //                       the mesh-coupled strip (depth-matched, no
  //                       LOD selection needed).
  //   - mode.wireframe  : draw ONLY the BRep wireframe — the analytical
  //                       LOD ladder, refined per frame from the
  //                       camera's world-per-pixel, since there is no
  //                       triangulated face to anchor against.
  // The two modes are mutually exclusive at the UI level.
  if (!prog_edges_.valid() || !scene_) return;
  if (!mode.show_edges && !mode.wireframe) return;

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
  // the edge overlay doesn't pollute the depth buffer for the pivot pass.
  gl_.glDepthMask(GL_FALSE);

  // Dark line over warm surfaces reads as ink; intensity slider in
  // DisplayMode controls overall strength.
  const float a = std::clamp(mode.edge_intensity, 0.0f, 1.0f);
  const scene::vec4 ec{0.05f, 0.06f, 0.08f, a};
  gl_.glUniform4fv(loc_color, 1, &ec.x);

  gl_.glEnable(GL_BLEND);
  // Alpha-preserving blend: the edge ink blends into RGB normally, but
  // dst.alpha is left at whatever the surface/background pass wrote (1.0
  // in the default configuration). Using plain glBlendFunc here would
  // decay dst.alpha to (1 - src.a)*dst.a + src.a*src.a along every drawn
  // line, leaving the wireframe at ~0.77 alpha after one pass and lower
  // after overlaps — the OS compositor would then make those exact
  // pixels translucent and the desktop would bleed through the lines.
  gl_.glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                          GL_ZERO,      GL_ONE);
  // Drivers ignore this in core profile but it costs nothing and helps the
  // permissive ones (Mesa, some Intel) draw 1.2 px lines.
  gl_.glLineWidth(1.2f);

  // Primitive restart lets a single GL_LINE_STRIP draw cover the whole edge
  // buffer: each polyline is a contiguous run of indices, separated by the
  // 0xFFFFFFFF sentinel that the importer writes between strips
  // (OcctShapeToMesh.cpp). The previous GL_LINES layout repeated every
  // interior polyline vertex in two consecutive segments, so the joint
  // pixel rasterised twice and the alpha-blended ink stacked on itself —
  // visible as dark dots evenly spaced along every curved edge that
  // seemed to migrate as the camera rotated (joints have fixed world
  // positions but their screen projection slides). Strips render the joint
  // exactly once and the artifact is gone.
  gl_.glEnable(GL_PRIMITIVE_RESTART);
  gl_.glPrimitiveRestartIndex(0xFFFFFFFFu);

  // Camera-driven LOD selection used by the wireframe path. World-per-pixel
  // at the camera's target plane is the same formula in orthographic and
  // perspective: the ortho frustum's half-height is `distance *
  // tan(fov_y/2)` by construction (see Camera::projection), so the
  // screen-height-in-world is identical in both modes. Computed once per
  // frame and reused for every node.
  constexpr float kPixelBudget = 1.0f;
  const auto& cam = scene_->camera;
  const float screen_height_world =
    2.0f * std::max(cam.distance, 1e-6f) * std::tan(0.5f * cam.fov_y);
  const float world_per_pixel =
    screen_height_world / std::max(static_cast<float>(viewport_h_), 1.0f);
  const float deflection_budget = kPixelBudget * world_per_pixel;

  auto select_lod = [&](const std::vector<EdgeLodGpu>& lods) -> const EdgeLodGpu* {
    // Tiers are stored coarsest first. Pick the COARSEST tier whose chord
    // deflection still fits the per-pixel budget — going finer would draw
    // more segments without any visual change. If we are zoomed in past
    // the whole ladder, fall through to the finest non-empty tier.
    const EdgeLodGpu* chosen = nullptr;
    for (const auto& lod : lods) {
      if (lod.index_count == 0) continue;
      if (lod.linear_deflection <= deflection_budget) { chosen = &lod; break; }
    }
    if (!chosen) {
      for (auto it = lods.rbegin(); it != lods.rend(); ++it) {
        if (it->index_count != 0) { chosen = &*it; break; }
      }
    }
    return chosen;
  };

  for (const auto& node : scene_->nodes) {
    if (!node.visible || !node.mesh_index) continue;
    if (*node.mesh_index >= scene_->meshes.size()) continue;
    const auto& mesh_ptr = scene_->meshes[*node.mesh_index];
    if (!mesh_ptr) continue;
    auto mit = meshes_.find(mesh_ptr.get());
    if (mit == meshes_.end()) continue;
    const auto& g = mit->second;

    GLuint  vao   = 0;
    GLsizei count = 0;
    if (mode.wireframe) {
      // BRep wireframe: refined polylines from the analytical LOD ladder.
      const EdgeLodGpu* lod = select_lod(g.edge_lods);
      if (!lod) continue;
      vao   = lod->vao;
      count = lod->index_count;
    } else {
      // Shaded-with-edges overlay: mesh-coupled strip indices into the
      // surface VBO. Polygon offset on the surface (see render()) keeps
      // these edges in front of their faces without geometric divergence.
      if (g.strip_vao == 0 || g.strip_index_count == 0) continue;
      vao   = g.strip_vao;
      count = g.strip_index_count;
    }

    gl_.glUniformMatrix4fv(loc_model, 1, GL_FALSE,
                           glm::value_ptr(node.world_matrix));
    gl_.glBindVertexArray(vao);
    gl_.glDrawElements(GL_LINE_STRIP, count, GL_UNSIGNED_INT, nullptr);
  }
  gl_.glBindVertexArray(0);
  gl_.glDisable(GL_PRIMITIVE_RESTART);
  gl_.glDisable(GL_BLEND);
  gl_.glDepthMask(GL_TRUE);
}

int GLRendererImpl::clamp_msaa_samples(int requested) const {
  // 0 and 1 both mean "no MSAA"; the renderer skips the FBO entirely in
  // that case. Anything else is clamped to the GPU's reported max, so a
  // request for 16x on a 4x-only card silently degrades instead of failing.
  if (requested <= 1) return 0;
  if (max_samples_ <= 1) return 0;
  return std::min(requested, max_samples_);
}

void GLRendererImpl::release_msaa_target() {
  if (fbo_msaa_)     gl_.glDeleteFramebuffers(1, &fbo_msaa_);
  if (rb_color_msaa_) gl_.glDeleteRenderbuffers(1, &rb_color_msaa_);
  if (rb_depth_msaa_) gl_.glDeleteRenderbuffers(1, &rb_depth_msaa_);
  fbo_msaa_             = 0;
  rb_color_msaa_        = 0;
  rb_depth_msaa_        = 0;
  msaa_samples_current_ = 0;
  fbo_w_                = 0;
  fbo_h_                = 0;
}

void GLRendererImpl::ensure_msaa_target(int width_px, int height_px, int samples) {
  if (samples <= 0) { release_msaa_target(); return; }
  if (fbo_msaa_ != 0 &&
      msaa_samples_current_ == samples &&
      fbo_w_ == width_px && fbo_h_ == height_px) {
    return;
  }
  release_msaa_target();

  gl_.glGenFramebuffers(1, &fbo_msaa_);
  gl_.glGenRenderbuffers(1, &rb_color_msaa_);
  gl_.glGenRenderbuffers(1, &rb_depth_msaa_);

  gl_.glBindRenderbuffer(GL_RENDERBUFFER, rb_color_msaa_);
  gl_.glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8,
                                       width_px, height_px);
  gl_.glBindRenderbuffer(GL_RENDERBUFFER, rb_depth_msaa_);
  gl_.glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples,
                                       GL_DEPTH24_STENCIL8,
                                       width_px, height_px);
  gl_.glBindRenderbuffer(GL_RENDERBUFFER, 0);

  gl_.glBindFramebuffer(GL_FRAMEBUFFER, fbo_msaa_);
  gl_.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                GL_RENDERBUFFER, rb_color_msaa_);
  gl_.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                GL_RENDERBUFFER, rb_depth_msaa_);
  GLenum status = gl_.glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    CADLY_LOG_ERROR("MSAA FBO incomplete (status=0x{:x}); falling back to "
                    "single-sample default framebuffer.", status);
    gl_.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    release_msaa_target();
    return;
  }
  gl_.glBindFramebuffer(GL_FRAMEBUFFER, 0);

  msaa_samples_current_ = samples;
  fbo_w_ = width_px;
  fbo_h_ = height_px;
}

void GLRendererImpl::render(const renderer::DisplayMode& mode) {
  if (!initialised_) initialize();
  if (!initialised_) return;

  // Snapshot the host's default framebuffer before we redirect drawing into
  // our MSAA FBO. QOpenGLWidget renders into a Qt-managed FBO (not 0), so
  // querying GL_FRAMEBUFFER_BINDING is the only portable way to recover
  // it for the resolve blit at end of frame.
  GLint default_fbo = 0;
  gl_.glGetIntegerv(GL_FRAMEBUFFER_BINDING, &default_fbo);

  const int wanted_samples = clamp_msaa_samples(mode.msaa_samples);
  const bool use_msaa = wanted_samples > 0;
  if (use_msaa) {
    ensure_msaa_target(viewport_w_, viewport_h_, wanted_samples);
    if (fbo_msaa_ != 0) {
      gl_.glBindFramebuffer(GL_FRAMEBUFFER, fbo_msaa_);
    }
  } else {
    release_msaa_target();
  }

  // RAII guard: no matter which `return` path render() takes, the MSAA
  // colour buffer gets resolved into the host's default FBO before this
  // function leaves. Without the RAII wrapper every early-out below would
  // need to remember to do this blit explicitly.
  struct ResolveOnExit {
    GLFunctions& gl;
    GLuint       src;
    GLuint       dst;
    int          w;
    int          h;
    bool         active;
    ~ResolveOnExit() {
      if (!active) return;
      gl.glBindFramebuffer(GL_READ_FRAMEBUFFER, src);
      gl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst);
      // GL_NEAREST is required by spec for an MSAA→single-sample resolve
      // (and is also fine when both ends are single-sample, which never
      // happens here but is worth remembering). GL_COLOR_BUFFER_BIT alone
      // is enough — no later pass reads our depth buffer back, so paying
      // to resolve depth would be pure waste.
      gl.glBlitFramebuffer(0, 0, w, h, 0, 0, w, h,
                           GL_COLOR_BUFFER_BIT, GL_NEAREST);
      // Restore "default FBO is bound" so subsequent host code (Qt's
      // QOpenGLWidget swap, screenshots, …) doesn't see our MSAA FBO.
      gl.glBindFramebuffer(GL_FRAMEBUFFER, dst);
    }
  } resolve{gl_, fbo_msaa_, static_cast<GLuint>(default_fbo),
            viewport_w_, viewport_h_, use_msaa && fbo_msaa_ != 0};

  gl_.glViewport(0, 0, viewport_w_, viewport_h_);
  gl_.glClearDepthf(1.0f);
  gl_.glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

  draw_background(mode);

  if (!scene_ || scene_->nodes.empty()) {
    draw_pivot(mode);
    draw_scale_bar();
    return;
  }

  // Camera::aspect is owned by the camera. The host is expected to keep it
  // in sync with the viewport (the Qt host does this in CameraController::
  // set_viewport); the renderer only reads it here.
  update_frame_uniforms();

  if (!prog_pbr_.valid()) {
    CADLY_LOG_WARN("PBR program not available; skipping mesh draw.");
    draw_pivot(mode);
    draw_scale_bar();
    return;
  }

  // Wireframe mode shows ONLY the BRep wireframe (handled by draw_edges
  // below using the analytical LOD ladder). Skip the entire shaded surface
  // pass so triangle-mesh edges are never drawn implicitly — the old
  // behaviour of flipping glPolygonMode to GL_LINE produced exactly that
  // and is the thing this mode is meant to avoid. The triangle-mesh
  // overlay is also skipped here: it relies on the filled surface to
  // occlude back-of-part triangles, and without it the overlay penetrates
  // the silhouette and lets the background show through. The UI keeps the
  // two modes mutually exclusive so this branch should never see
  // show_triangle_mesh set, but guard anyway.
  if (mode.wireframe) {
    draw_edges(mode);
    draw_pivot(mode);
    draw_scale_bar();
    return;
  }

  gl_.glUseProgram(prog_pbr_.id());

  // When any line overlay is on (BRep edges or the triangle-mesh debug
  // view), push the surface fragments slightly away from the camera in
  // depth space. The lines keep their true depth, so:
  //   - lines on a visible face pass the depth test against the pushed-back
  //     surface and draw on top of it (no Z-fighting),
  //   - lines on the BACK of the part still fail the depth test against
  //     the front-facing surface — i.e. hidden lines stay hidden.
  // This is the classic CAD "shaded with edges" recipe.
  const bool line_overlay = mode.show_edges || mode.show_triangle_mesh;
  if (line_overlay) {
    gl_.glEnable(GL_POLYGON_OFFSET_FILL);
    // Mesh-coupled BRep edges and triangle-mesh lines both index the
    // surface VBO directly, so lines and faces have identical depth
    // values BEFORE this offset is applied — (1, 1) is plenty to give
    // the line a stable win on every visible face while still letting
    // the front-facing surface occlude back-of-part lines.
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

      // Draw both faces when either the geometry isn't a closed solid
      // (mesh.double_sided — set by the importer for IGES surface quilts /
      // open shells, whose patch winding is arbitrary) or the material is
      // intrinsically two-sided. Otherwise keep the cheaper single-sided cull.
      if (mesh_ptr->double_sided || m.double_sided) {
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

  if (line_overlay) {
    gl_.glDisable(GL_POLYGON_OFFSET_FILL);
  }

  draw_triangle_mesh(mode);
  draw_edges(mode);
  draw_pivot(mode);
  draw_scale_bar();
}

void GLRendererImpl::shutdown() {
  if (!initialised_) return;
  for (auto& [_, g] : meshes_) {
    free_mesh_gpu(g);
  }
  meshes_.clear();
  release_msaa_target();
  if (ubo_frame_)      gl_.glDeleteBuffers(1, &ubo_frame_);
  if (vbo_overlay_)    gl_.glDeleteBuffers(1, &vbo_overlay_);
  if (vao_background_) gl_.glDeleteVertexArrays(1, &vao_background_);
  if (vao_pivot_)      gl_.glDeleteVertexArrays(1, &vao_pivot_);
  if (vao_overlay_)    gl_.glDeleteVertexArrays(1, &vao_overlay_);
  if (vao_quad_)       gl_.glDeleteVertexArrays(1, &vao_quad_);
  if (env_cube_)        gl_.glDeleteTextures(1, &env_cube_);
  if (irradiance_cube_) gl_.glDeleteTextures(1, &irradiance_cube_);
  if (prefilter_cube_)  gl_.glDeleteTextures(1, &prefilter_cube_);
  if (brdf_lut_)        gl_.glDeleteTextures(1, &brdf_lut_);
  ubo_frame_ = vbo_overlay_ = 0;
  vao_background_ = vao_pivot_ = vao_overlay_ = vao_quad_ = 0;
  env_cube_ = irradiance_cube_ = prefilter_cube_ = brdf_lut_ = 0;
  prog_pbr_.destroy(gl_);
  prog_background_.destroy(gl_);
  prog_pivot_.destroy(gl_);
  prog_overlay_.destroy(gl_);
  prog_edges_.destroy(gl_);
  prog_env_capture_.destroy(gl_);
  prog_irradiance_.destroy(gl_);
  prog_prefilter_.destroy(gl_);
  prog_brdf_lut_.destroy(gl_);
  initialised_ = false;
}

} // namespace

std::unique_ptr<renderer::IRenderer> make_gl_renderer(GLLoadProc load_proc) {
  return std::make_unique<GLRendererImpl>(std::move(load_proc));
}

} // namespace cadly::renderer_gl

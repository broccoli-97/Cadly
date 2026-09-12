#include "SectionPass.h"

#include "cadly/platform/Log.h"
#include "cadly/renderer/SectionPlane.h"
#include "cadly/scene/Mesh.h"
#include "cadly/scene/Node.h"
#include "cadly/scene/Scene.h"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace cadly::renderer_gl::detail {

namespace {

// Cross-section polygons top out at 12 vertices (see plane_box_cross_section),
// drawn as a fan.
constexpr int kMaxPolyVerts = 12;

// Hatch strength mixes ink into the fill independently of surface opacity.
constexpr float kHatchShadeTone = 0.40f;
constexpr float kHatchShadeAlpha = 0.70f;
constexpr float kHatchInkAlpha = 0.46f;
constexpr float kHatchPitchPx = 13.0f;
constexpr float kTranslucentCapOpacity = 0.35f;

// Handle geometry, in a unit space the draw scales to a constant pixel length:
// a shaft along Z with a cone at each end. Double-headed because the drag is
// bidirectional — a single arrow invites "which way does this go?", and the two
// cones also give the hit-test a longer, easier target than a single tip.
//
// The tips sit at exactly +/-1, so the uniform model scale IS the world reach of
// the handle — which is what lets renderer::section_handle_segment() reproduce
// the same span for the host's hit-test from the pixel length alone.
constexpr float kShaftHalf   = 0.62f;   // shaft reaches +/- this along Z
constexpr float kConeHalf    = 1.0f;    // cone tips at +/- this
constexpr float kShaftRadius = 0.055f;
constexpr float kConeRadius  = 0.16f;
constexpr int   kConeSegments = 12;

// Pixel length of the handle. Large enough to grab without hunting, small
// enough not to dominate the part. Shared with the host's hit-test.
constexpr float kHandlePx = renderer::kSectionHandlePx;

// Rotate-ring geometry. The ring is drawn as a flat annulus of unit outer
// radius; the model matrix scales it to kSectionRingPx on screen, so a constant
// stroke in pixels is a constant FRACTION of the unit radius here.
constexpr float kRingStrokePx = 3.0f;
constexpr float kRingInner    = 1.0f - kRingStrokePx / renderer::kSectionRingPx;
constexpr int   kRingSegments = 96;

void push(std::vector<scene::vec3>& out, const scene::vec3& a,
          const scene::vec3& b, const scene::vec3& c) {
  out.push_back(a);
  out.push_back(b);
  out.push_back(c);
}

// Flat annulus in the XY plane, as a triangle soup. Culling is off for the
// manipulator, so winding does not matter.
std::vector<scene::vec3> build_ring_geometry() {
  std::vector<scene::vec3> v;
  v.reserve(static_cast<std::size_t>(kRingSegments) * 6);
  const float two_pi = 6.28318530718f;
  for (int i = 0; i < kRingSegments; ++i) {
    const float a0 = two_pi * static_cast<float>(i) / kRingSegments;
    const float a1 = two_pi * static_cast<float>(i + 1) / kRingSegments;
    const scene::vec2 d0{std::cos(a0), std::sin(a0)};
    const scene::vec2 d1{std::cos(a1), std::sin(a1)};
    const scene::vec3 o0{d0.x, d0.y, 0.0f};
    const scene::vec3 o1{d1.x, d1.y, 0.0f};
    const scene::vec3 i0{d0.x * kRingInner, d0.y * kRingInner, 0.0f};
    const scene::vec3 i1{d1.x * kRingInner, d1.y * kRingInner, 0.0f};
    push(v, i0, o0, o1);
    push(v, i0, o1, i1);
  }
  return v;
}

// Cone + shaft as a triangle soup, mirrored on Z. No index buffer: the whole
// thing is a few dozen triangles uploaded once at startup.
std::vector<scene::vec3> build_handle_geometry() {
  std::vector<scene::vec3> v;
  const float two_pi = 6.28318530718f;

  for (int sign_i = 0; sign_i < 2; ++sign_i) {
    const float s = (sign_i == 0) ? 1.0f : -1.0f;
    const scene::vec3 tip{0.0f, 0.0f, kConeHalf * s};
    const float base_z = kShaftHalf * s;

    for (int i = 0; i < kConeSegments; ++i) {
      const float a0 = two_pi * static_cast<float>(i) / kConeSegments;
      const float a1 = two_pi * static_cast<float>(i + 1) / kConeSegments;
      const scene::vec3 r0{std::cos(a0) * kConeRadius,
                           std::sin(a0) * kConeRadius, base_z};
      const scene::vec3 r1{std::cos(a1) * kConeRadius,
                           std::sin(a1) * kConeRadius, base_z};
      // Cone wall and its base disc. Culling is off for the handle, so winding
      // does not matter here.
      push(v, tip, r0, r1);
      push(v, scene::vec3(0.0f, 0.0f, base_z), r1, r0);

      // Shaft wall for this segment, from the origin out to the cone base.
      const scene::vec3 s0{std::cos(a0) * kShaftRadius,
                           std::sin(a0) * kShaftRadius, 0.0f};
      const scene::vec3 s1{std::cos(a1) * kShaftRadius,
                           std::sin(a1) * kShaftRadius, 0.0f};
      const scene::vec3 t0{s0.x, s0.y, base_z};
      const scene::vec3 t1{s1.x, s1.y, base_z};
      push(v, s0, s1, t1);
      push(v, s0, t1, t0);
    }
  }
  return v;
}

// Screen scale, matching the formula draw_edges and draw_scale_bar use. Works in
// both projection modes because Camera::projection() derives the orthographic
// half-height from `distance * tan(fov_y/2)` as well.
float world_per_pixel(const scene::Camera& cam, int viewport_h) {
  const float screen_height_world =
    2.0f * std::max(cam.distance, 1e-6f) * std::tan(0.5f * cam.fov_y);
  return screen_height_world / std::max(static_cast<float>(viewport_h), 1.0f);
}

} // namespace

bool SectionPass::initialize(GLFunctions& gl, GLuint frame_binding) {
  auto vs = load_shader_source("section.vert");
  auto fs = load_shader_source("section.frag");
  if (!vs || !fs) {
    CADLY_LOG_WARN("Section shaders missing; section view will be unavailable.");
    return false;
  }
  // Built here rather than through GLRendererImpl's program manifest on purpose:
  // that manifest ANDs every program's success into one flag, so a typo in a
  // section shader would fail initialize() for the WHOLE renderer — and since
  // render() retries initialize() every paint, that means a black viewport at
  // 100% CPU. Degrading to "no section mode" is the better failure.
  if (!prog_section_.build(gl, *vs, *fs, "section")) {
    CADLY_LOG_WARN("Section program failed to build; section view unavailable.");
    return false;
  }
  const GLuint idx = prog_section_.uniform_block(gl, "FrameBlock");
  if (idx != GL_INVALID_INDEX) {
    gl.glUniformBlockBinding(prog_section_.id(), idx, frame_binding);
  }

  gl.glGenVertexArrays(1, &vao_poly_);
  gl.glGenBuffers(1, &vbo_poly_);
  gl.glBindVertexArray(vao_poly_);
  gl.glBindBuffer(GL_ARRAY_BUFFER, vbo_poly_);
  gl.glBufferData(GL_ARRAY_BUFFER, sizeof(scene::vec3) * kMaxPolyVerts,
                  nullptr, GL_DYNAMIC_DRAW);
  gl.glEnableVertexAttribArray(0);
  gl.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(scene::vec3),
                           nullptr);

  const std::vector<scene::vec3> handle = build_handle_geometry();
  handle_verts_ = static_cast<GLsizei>(handle.size());
  gl.glGenVertexArrays(1, &vao_handle_);
  gl.glGenBuffers(1, &vbo_handle_);
  gl.glBindVertexArray(vao_handle_);
  gl.glBindBuffer(GL_ARRAY_BUFFER, vbo_handle_);
  gl.glBufferData(GL_ARRAY_BUFFER,
                  static_cast<GLsizeiptr>(sizeof(scene::vec3) * handle.size()),
                  handle.data(), GL_STATIC_DRAW);
  gl.glEnableVertexAttribArray(0);
  gl.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(scene::vec3),
                           nullptr);

  const std::vector<scene::vec3> ring = build_ring_geometry();
  ring_verts_ = static_cast<GLsizei>(ring.size());
  gl.glGenVertexArrays(1, &vao_ring_);
  gl.glGenBuffers(1, &vbo_ring_);
  gl.glBindVertexArray(vao_ring_);
  gl.glBindBuffer(GL_ARRAY_BUFFER, vbo_ring_);
  gl.glBufferData(GL_ARRAY_BUFFER,
                  static_cast<GLsizeiptr>(sizeof(scene::vec3) * ring.size()),
                  ring.data(), GL_STATIC_DRAW);
  gl.glEnableVertexAttribArray(0);
  gl.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(scene::vec3),
                           nullptr);

  gl.glBindVertexArray(0);
  gl.glBindBuffer(GL_ARRAY_BUFFER, 0);

  built_ = true;
  return true;
}

void SectionPass::shutdown(GLFunctions& gl) {
  prog_section_.destroy(gl);
  if (vbo_poly_)   gl.glDeleteBuffers(1, &vbo_poly_);
  if (vbo_handle_) gl.glDeleteBuffers(1, &vbo_handle_);
  if (vbo_ring_)   gl.glDeleteBuffers(1, &vbo_ring_);
  if (vao_poly_)   gl.glDeleteVertexArrays(1, &vao_poly_);
  if (vao_handle_) gl.glDeleteVertexArrays(1, &vao_handle_);
  if (vao_ring_)   gl.glDeleteVertexArrays(1, &vao_ring_);
  vbo_poly_ = vbo_handle_ = vbo_ring_ = 0;
  vao_poly_ = vao_handle_ = vao_ring_ = 0;
  poly_count_ = 0;
  handle_verts_ = 0;
  ring_verts_ = 0;
  built_ = false;
  active_ = false;
  cap_mask_ready_ = false;
  stencil_bits_ = -1;
  stencil_probed_fbo_ = -1;
  clip_plane_ = scene::vec4(0.0f);
}

void SectionPass::begin_frame(const renderer::DisplayMode& mode,
                              const scene::Scene& scene) {
  active_     = false;
  cap_mask_ready_ = false;
  clip_plane_ = scene::vec4(0.0f);
  poly_count_ = 0;

  if (!built_ || !mode.section_enabled) return;
  if (!scene.world_bounds.valid()) return;

  bounds_      = scene.world_bounds;
  plane_       = mode.section;
  clip_plane_  = plane_.equation(bounds_);
  plane_n_     = plane_.unit_normal();
  plane_origin_ = plane_.anchor(bounds_);
  plane_.basis(plane_u_, plane_v_);

  // Nudge the cap fill a hair toward the kept side. A model face lying exactly
  // ON the section plane is kept (the clip test is >= 0) and would then render at
  // precisely the cap's depth — Z-fighting across the whole face, and users drag
  // the plane onto planar features all the time. Biasing the cap makes such a
  // face strictly nearer the camera, so it wins outright and the cap stays
  // hidden behind it. Scaled to the model so it is invisible at any unit size.
  cap_epsilon_ = std::max(bounds_.radius() * 1e-4f, 1e-6f);

  active_ = true;
}

bool SectionPass::cap_available(GLFunctions& gl) {
  if (!built_) return false;
  // Probe once per framebuffer. The offscreen MSAA target allocates
  // DEPTH24_STENCIL8 itself so it always has stencil; the no-MSAA path draws into
  // the host's framebuffer (for QOpenGLWidget, a Qt-managed FBO — not 0), whose
  // stencil depends on the host having asked for it, so it is worth verifying
  // rather than assuming. Note glGetIntegerv(GL_STENCIL_BITS) is not legal in a
  // core profile, hence the attachment query.
  GLint bound = 0;
  gl.glGetIntegerv(GL_FRAMEBUFFER_BINDING, &bound);
  if (stencil_probed_fbo_ != bound) {
    stencil_probed_fbo_ = bound;
    stencil_bits_ = 0;
    // The attachment enum differs by framebuffer: GL_STENCIL names the DEFAULT
    // framebuffer's stencil, while a user FBO wants GL_STENCIL_ATTACHMENT.
    // Asking the wrong one is an INVALID_OPERATION that silently leaves `type`
    // alone — which reads as "no stencil" and would disable the cap on the very
    // path (our own MSAA FBO) that definitely has one. Deliberately not
    // GL_DEPTH_STENCIL_ATTACHMENT: that query is legal only when the depth and
    // stencil attachments are the same object.
    const GLenum attachment =
      (bound == 0) ? static_cast<GLenum>(GL_STENCIL)
                   : static_cast<GLenum>(GL_STENCIL_ATTACHMENT);
    GLint type = GL_NONE;
    gl.glGetFramebufferAttachmentParameteriv(
      GL_FRAMEBUFFER, attachment, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
    if (type != GL_NONE) {
      gl.glGetFramebufferAttachmentParameteriv(
        GL_FRAMEBUFFER, attachment, GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE,
        &stencil_bits_);
    }
  }
  if (stencil_bits_ <= 0) {
    if (!stencil_warned_) {
      stencil_warned_ = true;
      CADLY_LOG_WARN("No stencil bits on the active framebuffer; the section "
                     "cut will be left uncapped.");
    }
    return false;
  }
  return true;
}

void SectionPass::enable_clip(GLFunctions& gl) {
  if (!active_) return;
  // While this is enabled, EVERY program that draws must write
  // gl_ClipDistance[0] — an unwritten clip distance is undefined per spec, not
  // zero, so a program that skips it can have its geometry clipped arbitrarily.
  // Inside the enabled window that means prog_pbr_, prog_edges_ (which also
  // carries the triangle-mesh overlay), and prog_silhouette_. The cap, the
  // gizmo, and the screen-space overlays all draw outside it.
  gl.glEnable(GL_CLIP_DISTANCE0);
}

void SectionPass::disable_clip(GLFunctions& gl) {
  gl.glDisable(GL_CLIP_DISTANCE0);
}

void SectionPass::upload_cross_section(GLFunctions& gl, float extra_offset) {
  std::array<scene::vec3, kMaxPolyVerts> poly{};
  poly_count_ = renderer::plane_box_cross_section(plane_, bounds_, extra_offset,
                                                  poly.data(), kMaxPolyVerts);
  if (poly_count_ < 3) { poly_count_ = 0; return; }
  gl.glBindBuffer(GL_ARRAY_BUFFER, vbo_poly_);
  gl.glBufferSubData(GL_ARRAY_BUFFER, 0,
                     static_cast<GLsizeiptr>(sizeof(scene::vec3) * poly_count_),
                     poly.data());
  gl.glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void SectionPass::draw_cap(GLFunctions& gl, const scene::Scene& scene,
                           const renderer::DisplayMode& mode,
                           GLProgram& edges_program,
                           const SolidLookup& lookup,
                           int viewport_h) {
  if (!active_ || !prog_section_.valid()) return;
  const bool translucent = mode.section_translucent && !mode.hidden_line;

  upload_cross_section(gl, cap_epsilon_);
  if (poly_count_ < 3) return;   // plane is clear of the model

  // ---- Pass A: count front vs back faces of the CLIPPED geometry ------------
  //
  // prog_edges_ is borrowed rather than given a program of its own. It is the
  // only position-only program that already binds FrameBlock and takes u_model,
  // it reads attribute 0 from the same surface VAO (exactly as the triangle-mesh
  // overlay does), and — the part that matters — it writes gl_ClipDistance[0].
  // The counting pass MUST see clipped geometry: uncut, every closed solid
  // balances to zero everywhere and no cap is ever produced.
  if (!edges_program.valid()) return;
  gl.glUseProgram(edges_program.id());
  const GLint loc_model = edges_program.uniform(gl, "u_model");
  gl.glUniform1f(edges_program.uniform(gl, "u_view_bias"), 0.0f);

  gl.glEnable(GL_STENCIL_TEST);
  gl.glStencilMask(0xFF);
  gl.glClearStencil(0);
  gl.glClear(GL_STENCIL_BUFFER_BIT);
  gl.glStencilFunc(GL_ALWAYS, 0, 0xFF);
  gl.glStencilOpSeparate(GL_FRONT, GL_KEEP, GL_KEEP, GL_INCR_WRAP);
  gl.glStencilOpSeparate(GL_BACK,  GL_KEEP, GL_KEEP, GL_DECR_WRAP);
  gl.glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
  // Depth test off so every surface along the ray contributes to the count, not
  // just the nearest one. That also suppresses depth writes, so no depth mask
  // change is needed.
  gl.glDisable(GL_DEPTH_TEST);
  gl.glDisable(GL_CULL_FACE);
  gl.glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

  for (const auto& node : scene.nodes) {
    if (!node.visible || !node.mesh_index) continue;
    // Ghosted parts render as a veil that writes no depth, so capping them would
    // paint a solid patch over a part the user asked to fade out.
    if (node.ghosted) continue;
    // Only parts the plane actually cuts can change the mask: a part wholly on
    // the kept side contributes a matched front/back pair (net zero) and one
    // wholly cut away contributes no fragments. On a big assembly this reduces
    // the pass from every part to a handful.
    if (!renderer::box_straddles_plane(node.world_bounds, clip_plane_)) continue;
    if (*node.mesh_index >= scene.meshes.size()) continue;
    const auto& mesh_ptr = scene.meshes[*node.mesh_index];
    if (!mesh_ptr) continue;
    // Not a closed solid, so front/back counting is meaningless — and there is
    // no volume to cap in the first place. (See the class comment for why this
    // flag does not catch every open shell.)
    if (mesh_ptr->double_sided) continue;
    const SolidDraw solid = lookup(*mesh_ptr);
    if (solid.vao == 0 || solid.index_count == 0) continue;

    gl.glUniformMatrix4fv(loc_model, 1, GL_FALSE,
                          glm::value_ptr(node.world_matrix));
    gl.glFrontFace(glm::determinant(scene::mat3(node.world_matrix)) < 0.0f
                     ? GL_CW : GL_CCW);
    gl.glBindVertexArray(solid.vao);
    // One drawcall for the whole mesh: materials are irrelevant to a count, so
    // the per-submesh split buys nothing here.
    gl.glDrawElements(GL_TRIANGLES, solid.index_count, GL_UNSIGNED_INT, nullptr);
  }
  gl.glBindVertexArray(0);
  gl.glFrontFace(GL_CCW);

  // ---- Pass B: fill the marked pixels with the cut face --------------------
  gl.glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  gl.glEnable(GL_DEPTH_TEST);
  gl.glDepthMask(translucent ? GL_FALSE : GL_TRUE);
  gl.glStencilFunc(GL_NOTEQUAL, 0, 0xFF);
  // Leave the write mask alone and make the ops no-ops instead. glStencilMask(0)
  // would work here too, but it is also what gates glClear(GL_STENCIL_BUFFER_BIT)
  // — leak a zero mask and the clear silently stops working forever.
  gl.glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
  // Clipping off: the polygon sits on (or a hair past) the plane, where the clip
  // distance is ~0 and the comparison is a coin flip.
  gl.glDisable(GL_CLIP_DISTANCE0);

  scene::vec4 fill(mode.section_cap_color,
                   translucent ? kTranslucentCapOpacity : 1.0f);
  scene::vec4 hatch(0.0f, 0.0f, 0.0f, 0.0f);
  float pitch = 0.0f;

  const bool want_hatch = mode.section_hatch || mode.hidden_line;
  if (mode.hidden_line) {
    // Hidden-line mode: the cap has to be the same paper as the surrounding
    // field, and pbr.frag gamma-encodes it in the shader, so match that exactly
    // or the cap reads as a patch of slightly-off grey.
    const scene::vec3 paper =
      glm::pow(glm::clamp(mode.hidden_line_color, 0.0f, 1.0f),
               scene::vec3(1.0f / 2.2f));
    fill = scene::vec4(paper, 1.0f);
    // ...and because the shell sets hidden_line_color to the background colour,
    // paper-on-paper would be indistinguishable from a hole. The hatch is the
    // only thing that makes the cut readable here, so it is not optional in this
    // mode — the toggle governs shaded mode, where the solid fill already reads.
    hatch = scene::vec4(0.0f, 0.0f, 0.0f, kHatchInkAlpha);
  } else {
    // Darken the fill for the strokes so the hatch reads as incised rather than
    // as a second colour competing with the part.
    hatch = scene::vec4(mode.section_cap_color * kHatchShadeTone,
                        kHatchShadeAlpha);
  }
  if (want_hatch) {
    // Constant screen pitch, so the hatch neither dissolves when zoomed out nor
    // spreads into stripes when zoomed in.
    pitch = kHatchPitchPx * world_per_pixel(scene.camera, viewport_h);
  }

  // A translucent cap runs after the retained geometry, including isolate
  // ghosts. Preserve framebuffer alpha so the window itself stays opaque.
  if (translucent) {
    gl.glEnable(GL_BLEND);
    gl.glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                           GL_ZERO, GL_ONE);
  } else {
    gl.glDisable(GL_BLEND);
  }

  gl.glUseProgram(prog_section_.id());
  const scene::mat4 identity(1.0f);
  gl.glUniformMatrix4fv(prog_section_.uniform(gl, "u_model"), 1, GL_FALSE,
                        glm::value_ptr(identity));
  gl.glUniform3fv(prog_section_.uniform(gl, "u_plane_origin"), 1,
                  &plane_origin_.x);
  gl.glUniform3fv(prog_section_.uniform(gl, "u_plane_u"), 1, &plane_u_.x);
  gl.glUniform3fv(prog_section_.uniform(gl, "u_plane_v"), 1, &plane_v_.x);
  gl.glUniform4fv(prog_section_.uniform(gl, "u_color"), 1, &fill.x);
  gl.glUniform4fv(prog_section_.uniform(gl, "u_hatch_color"), 1, &hatch.x);
  gl.glUniform1f (prog_section_.uniform(gl, "u_hatch_pitch"), pitch);

  gl.glBindVertexArray(vao_poly_);
  gl.glDrawArrays(GL_TRIANGLE_FAN, 0, poly_count_);
  gl.glBindVertexArray(0);
  cap_mask_ready_ = true;

  // ---- Restore the renderer's baseline -------------------------------------
  // Leaks here corrupt the NEXT frame, which is the hardest kind to trace: a
  // stuck colour mask silently defeats glClear(GL_COLOR_BUFFER_BIT), and a stuck
  // stencil test masks every later pass to the cap's footprint.
  gl.glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
  gl.glStencilFunc(GL_ALWAYS, 0, 0xFF);
  gl.glStencilMask(0xFF);
  gl.glDisable(GL_STENCIL_TEST);
  gl.glDisable(GL_BLEND);
  gl.glDepthMask(GL_TRUE);
  gl.glEnable(GL_CULL_FACE);
  gl.glEnable(GL_DEPTH_TEST);
  gl.glEnable(GL_CLIP_DISTANCE0);   // the model passes after us are still clipped
}

void SectionPass::draw_gizmo(GLFunctions& gl, const scene::Scene& scene,
                             const renderer::DisplayMode& mode,
                             int /*viewport_w*/, int viewport_h) {
  if (!active_ || !mode.section_show_plane || !prog_section_.valid()) return;

  upload_cross_section(gl, 0.0f);

  gl.glUseProgram(prog_section_.id());
  gl.glUniform3fv(prog_section_.uniform(gl, "u_plane_origin"), 1,
                  &plane_origin_.x);
  gl.glUniform3fv(prog_section_.uniform(gl, "u_plane_u"), 1, &plane_u_.x);
  gl.glUniform3fv(prog_section_.uniform(gl, "u_plane_v"), 1, &plane_v_.x);
  gl.glUniform1f (prog_section_.uniform(gl, "u_hatch_pitch"), 0.0f);

  // Alpha-preserving blend: dst.alpha is left untouched so a translucent overlay
  // cannot decay the window's alpha and let the compositor punch holes.
  gl.glEnable(GL_BLEND);
  gl.glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                         GL_ZERO,      GL_ONE);
  gl.glDisable(GL_CULL_FACE);
  gl.glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  // Depth-tested so the plane reads as living inside the model, but no depth
  // write: the manipulator must not occlude geometry or the parts behind it.
  gl.glDepthMask(GL_FALSE);

  const scene::mat4 identity(1.0f);
  gl.glUniformMatrix4fv(prog_section_.uniform(gl, "u_model"), 1, GL_FALSE,
                        glm::value_ptr(identity));

  const scene::vec3 guide_color = mode.section_cap_color * 0.60f;
  if (poly_count_ >= 3) {
    // The preview is slightly in front of the biased cap. Exclude cut material
    // with this frame's stencil so showing the plane cannot wash out the hatch.
    // Wireframe and unavailable caps have no mask; never reuse an older frame.
    if (cap_mask_ready_) {
      gl.glEnable(GL_STENCIL_TEST);
      gl.glStencilFunc(GL_EQUAL, 0, 0xFF);
      gl.glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    }
    const scene::vec4 face(mode.section_cap_color, 0.09f);
    gl.glUniform4fv(prog_section_.uniform(gl, "u_color"), 1, &face.x);
    gl.glBindVertexArray(vao_poly_);
    gl.glDrawArrays(GL_TRIANGLE_FAN, 0, poly_count_);

    // Border, so the plane's extent is legible where the translucent fill sits
    // against a similarly-toned part.
    const scene::vec4 edge(guide_color, 0.80f);
    gl.glUniform4fv(prog_section_.uniform(gl, "u_color"), 1, &edge.x);
    gl.glLineWidth(1.0f);
    gl.glDrawArrays(GL_LINE_LOOP, 0, poly_count_);
    gl.glBindVertexArray(0);
    if (cap_mask_ready_) {
      gl.glStencilFunc(GL_ALWAYS, 0, 0xFF);
      gl.glDisable(GL_STENCIL_TEST);
    }
  }

  // The drag handle, on top of everything so it is always grabbable.
  if (vao_handle_ != 0 && handle_verts_ > 0) {
    const float wpp = world_per_pixel(scene.camera, viewport_h);

    // Depth test off for the whole manipulator: a handle buried inside the part
    // would be ungrabbable, and an unreachable manipulator is worse than one
    // that floats.
    gl.glDisable(GL_DEPTH_TEST);

    // Rotate rings first, so the translate arrow — the more common action, and
    // the one drawn inside them — stays legible where they cross. The rings do
    // not depth-sort against each other; a rotate gizmo's rings always overlap
    // somewhere, and no ordering is right from every angle.
    if (vao_ring_ != 0 && ring_verts_ > 0) {
      const float ring_scale = renderer::kSectionRingPx * wpp;
      gl.glBindVertexArray(vao_ring_);
      for (const renderer::SectionGizmoPart part :
           {renderer::SectionGizmoPart::RotateX,
            renderer::SectionGizmoPart::RotateY,
            renderer::SectionGizmoPart::RotateZ}) {
        // A ring whose axis has drifted onto the plane normal turns the plane
        // by nothing at all. Drawing it anyway would put a large circle on the
        // cut face that does nothing when dragged, which reads as a broken
        // gizmo — so it simply is not there.
        if (!renderer::section_ring_live(plane_, part)) continue;

        scene::vec3 centre, e0, e1;
        if (!renderer::section_ring_frame(plane_, bounds_, part, wpp, centre, e0,
                                          e1)) {
          continue;
        }
        // The unit annulus lives in XY, so columns 0 and 1 are the ring's own
        // in-plane axes and column 2 is its rotation axis.
        scene::mat4 model(1.0f);
        model[0] = scene::vec4(e0, 0.0f);
        model[1] = scene::vec4(e1, 0.0f);
        model[2] =
          scene::vec4(renderer::section_ring_axis(part) * ring_scale, 0.0f);
        model[3] = scene::vec4(centre, 1.0f);
        gl.glUniformMatrix4fv(prog_section_.uniform(gl, "u_model"), 1, GL_FALSE,
                              glm::value_ptr(model));

        const int idx = renderer::section_ring_index(part);
        const scene::vec3 base = renderer::kAxisColor[idx];
        const bool hot = mode.section_hot_part == part;
        // Hot rings wash toward white rather than switching hue: the axis
        // colour is the ring's identity and must survive the highlight.
        const scene::vec4 color =
          hot ? scene::vec4(glm::mix(base, scene::vec3(1.0f), 0.55f), 1.0f)
              : scene::vec4(base, 0.85f);
        gl.glUniform4fv(prog_section_.uniform(gl, "u_color"), 1, &color.x);
        gl.glDrawArrays(GL_TRIANGLES, 0, ring_verts_);
      }
      gl.glBindVertexArray(0);
    }

    const float scale = kHandlePx * wpp;
    // Basis with +Z along the plane normal, uniformly scaled to a constant
    // on-screen length.
    scene::mat4 model(1.0f);
    model[0] = scene::vec4(plane_u_ * scale, 0.0f);
    model[1] = scene::vec4(plane_v_ * scale, 0.0f);
    model[2] = scene::vec4(plane_n_ * scale, 0.0f);
    model[3] = scene::vec4(plane_origin_, 1.0f);
    gl.glUniformMatrix4fv(prog_section_.uniform(gl, "u_model"), 1, GL_FALSE,
                          glm::value_ptr(model));

    const scene::vec4 handle =
      mode.section_hot_part == renderer::SectionGizmoPart::Translate
        ? scene::vec4(1.0f, 1.0f, 1.0f, 0.98f)
        : scene::vec4(guide_color, 1.0f);
    gl.glUniform4fv(prog_section_.uniform(gl, "u_color"), 1, &handle.x);
    gl.glBindVertexArray(vao_handle_);
    gl.glDrawArrays(GL_TRIANGLES, 0, handle_verts_);
    gl.glBindVertexArray(0);
    gl.glEnable(GL_DEPTH_TEST);
  }

  gl.glDepthMask(GL_TRUE);
  gl.glEnable(GL_CULL_FACE);
  gl.glDisable(GL_BLEND);
}

} // namespace cadly::renderer_gl::detail

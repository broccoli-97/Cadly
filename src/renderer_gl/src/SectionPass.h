#pragma once

#include "GLFunctions.h"
#include "GLShader.h"

#include "cadly/renderer/RenderTypes.h"
#include "cadly/scene/Aabb.h"
#include "cadly/scene/Math.h"

#include <functional>

namespace cadly::scene { struct Mesh; struct Scene; }

namespace cadly::renderer_gl::detail {

// Minimal window onto the host's GPU mesh cache: the surface VAO and its total
// index count, which is all the stencil-counting pass needs. Keeps this pass
// ignorant of how the cache is keyed and of when uploads happen — by the time
// the cap runs, the opaque surface pass has already uploaded everything.
struct SolidDraw {
  GLuint  vao        {0};
  GLsizei index_count{0};
};
using SolidLookup = std::function<SolidDraw(const scene::Mesh&)>;

// Section view (剖切): the clip plane, the stencil-masked cap that fills the
// exposed cut face, and the manipulator (plane preview + drag handle).
//
// Packaged as a self-contained component rather than folded into GLRendererImpl,
// so the section feature owns its shaders, buffers, and GL state in one place and
// the host only gains a handful of guarded call sites. Every entry point below is
// a no-op while `active()` is false.
//
//
// THE CAP ALGORITHM
//
// Clipping alone makes a solid look like an empty shell — you see the inside of
// the far wall through the hole. Filling the cut needs the set of pixels where
// the plane passed through material, which the CPU cannot know without
// intersecting the plane against every triangle. The stencil buffer computes it
// per pixel instead:
//
//   Pass A (count): with clipping ON, colour and depth writes OFF, depth test
//     OFF and culling OFF, re-draw the surviving surface triangles, incrementing
//     the stencil on front faces and decrementing on back faces. A closed solid
//     entirely on the kept side contributes a matched pair and nets zero; one
//     the plane cut has lost its front face to the clip, so its back face is
//     unmatched and the count is nonzero.
//   Pass B (fill): draw the plane's cross-section polygon wherever the stencil
//     is nonzero. Opaque caps write colour and depth before the edge passes;
//     translucent caps blend after retained geometry without writing depth.
//     Keep this mask until the auxiliary plane draws so its tint excludes cut
//     material. Inner cavity surfaces have opposite winding and cancel the
//     outer shell's count, leaving real holes open in either display style.
//
// Counting, not parity. GL_INVERT would be one line shorter and is what most
// tutorials show, but it breaks as soon as two bodies are cut by the same plane:
// two unmatched back faces invert twice, the parity returns to even, and the cap
// vanishes on exactly the assemblies this feature exists for. Counting is
// additive, so N cut bodies give -N. (INCR_WRAP/DECR_WRAP means an exact
// multiple of 256 cut bodies on one pixel would alias back to zero. Every
// implementation with an 8-bit stencil shares that bound.)
//
// The test is `!= 0` rather than a sign test, which also makes mirrored
// instances free: a negative-determinant world matrix swaps front and back, so a
// cut body counts +1 instead of -1, and a fully-kept mirrored body still
// balances to zero either way.
//
// Depth ordering needs no sorting, and this is why. With the camera on the cut
// side, `dot(plane, p)` increases monotonically along every view ray, so all kept
// geometry lies at or beyond the plane's own depth — the cap always wins the
// depth test where the stencil says "cut". Orbit around to the kept side and the
// relation inverts: all kept geometry is nearer than the plane, so the cap always
// loses and the intact outer surface shows through. Correct in both cases,
// without a branch.
//
// Known limitation: the count assumes closed, consistently wound geometry.
// `Mesh::double_sided` marks the meshes the importer knows are not solids and
// those are skipped, but that flag is set per imported shape (see
// OcctShapeToMesh's is_cull_safe), so a compound of one solid plus loose sheet
// bodies is not flagged and its unmatched sheet faces can raise the count on
// rays that cut no material — a phantom cap plate. Nothing here can detect that;
// it needs a per-solid closedness flag from the importer.
class SectionPass {
public:
  bool initialize(GLFunctions& gl, GLuint frame_binding);
  void shutdown(GLFunctions& gl);

  // Resolve the frame's plane geometry from the display mode and scene bounds.
  // Pure CPU; call once per frame before any geometry pass. Everything else on
  // this class reads the state it computes.
  void begin_frame(const renderer::DisplayMode& mode, const scene::Scene& scene);

  // True when a section is enabled and resolvable (a valid plane and scene
  // bounds). Every draw/enable entry point below no-ops otherwise.
  bool active() const { return active_; }

  // The plane for the shared FrameBlock, in Ax+By+Cz+D form with the kept
  // material on the >= 0 side. All-zero when inactive, which reads as "no
  // clipping" even if a caller enables the clip distance by mistake.
  const scene::vec4& clip_plane() const { return clip_plane_; }

  // Whether the cap can be drawn at all. False when the currently-bound
  // framebuffer has no stencil bits, or the programs failed to build — the clip
  // still works and the cut is simply left open. Probes GL once per framebuffer.
  bool cap_available(GLFunctions& gl);

  void enable_clip(GLFunctions& gl);
  void disable_clip(GLFunctions& gl);

  // Stencil-count the cut, then fill it. `edges_program` is borrowed for the
  // counting pass — see the .cpp for why that is the right program to reuse.
  // `viewport_h` is in the same pixel space the renderer draws in (device
  // pixels), and only sets the hatch's screen pitch.
  void draw_cap(GLFunctions& gl, const scene::Scene& scene,
                const renderer::DisplayMode& mode,
                GLProgram& edges_program, const SolidLookup& lookup,
                int viewport_h);

  // The manipulator: translucent plane, border, the world-axis rotate rings,
  // and the drag handle.
  void draw_gizmo(GLFunctions& gl, const scene::Scene& scene,
                  const renderer::DisplayMode& mode,
                  int viewport_w, int viewport_h);

private:
  void upload_cross_section(GLFunctions& gl, float extra_offset);

  GLProgram prog_section_;

  // Cross-section polygon, re-uploaded whenever the plane or bounds change.
  GLuint  vao_poly_  {0};
  GLuint  vbo_poly_  {0};
  int     poly_count_{0};

  // Static unit geometry for the handle (double-headed arrow along +/-Z).
  GLuint  vao_handle_  {0};
  GLuint  vbo_handle_  {0};
  GLsizei handle_verts_{0};

  // Static unit geometry for one rotate ring: a flat annulus in XY, outer radius
  // 1. Drawn three times with different model matrices — the rings differ only
  // by which world axis they turn about. An annulus rather than GL_LINE_LOOP
  // because line widths above 1.0 are not required to be supported in a core
  // profile, and a one-pixel ring is neither visible nor grabbable.
  GLuint  vao_ring_  {0};
  GLuint  vbo_ring_  {0};
  GLsizei ring_verts_{0};

  bool   active_    {false};
  bool   built_     {false};
  bool   cap_mask_ready_{false};
  bool   stencil_warned_{false};
  GLint  stencil_bits_{-1};
  GLint  stencil_probed_fbo_{-1};

  scene::vec4 clip_plane_{0.0f};
  renderer::SectionPlane plane_{};
  scene::vec3 plane_origin_{0.0f};
  scene::vec3 plane_u_{1.0f, 0.0f, 0.0f};
  scene::vec3 plane_v_{0.0f, 1.0f, 0.0f};
  scene::vec3 plane_n_{0.0f, 0.0f, 1.0f};
  scene::Aabb bounds_ = scene::Aabb::empty();
  float       cap_epsilon_{0.0f};
};

} // namespace cadly::renderer_gl::detail

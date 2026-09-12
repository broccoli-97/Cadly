layout(std140) uniform FrameBlock {
  mat4 u_view;
  mat4 u_proj;
  mat4 u_view_proj;
  // Perspective: xyz is the eye position and w is 1. Orthographic: xyz is
  // the constant unit direction from the surface toward the viewer and w is
  // 0. Shading and view-dependent geometry must honor this distinction.
  vec4 u_view_ref;
  vec4 u_ambient;
  vec4 u_key_dir;
  vec4 u_key_color;
  vec4 u_fill_dir;
  vec4 u_fill_color;
  vec4 u_rim_dir;
  vec4 u_rim_color;
  // Section (剖切) plane in Ax+By+Cz+D form; material on the >= 0 side is kept.
  // Every vertex-processing stage that draws MODEL geometry assigns the dot
  // product of this plane with the vertex's world position (w = 1) to clip
  // distance 0 — see pbr.vert, edges.vert, and silhouette.geom for the actual
  // line. The host enables GL_CLIP_DISTANCE0 only while a section is active, so
  // the written value is simply ignored the rest of the time and needs no
  // separate "enabled" uniform. Zero here means "no clipping" even if the host
  // enables the distance by mistake, because clipping needs a NEGATIVE value.
  //
  // Deliberately NOT wrapped in a shared helper file: this block is declared in
  // exactly one place, and the shader loader's include expander has no
  // include-once guard (it only detects cycles on the active stack), so a helper
  // that pulled this file in would double-declare the block wherever both were
  // pulled in together. Note also that this header is expanded into EVERY
  // shader, so keep example code out of it — the shader_source test greps
  // expanded sources for the clip-distance write, and a sample line here would
  // make every shader look like it performs one.
  vec4 u_clip_plane;
};

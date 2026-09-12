#version 410 core

// BRep edge polyline pass. Only consumes positions — colour and bias are
// uniforms — so it reuses the host edge VBO directly. The edge is pulled
// slightly toward the camera in view space (`u_view_bias`) so it sits on
// top of the shaded surface without falling into per-pixel Z-fighting.
// Doing the bias in view space (rather than glPolygonOffset on the surface
// pass) keeps the offset perceptually constant across distances and play
// well with depth-tested transparency in future passes.

layout(location = 0) in vec3 a_position;

#include "common/frame_block.glsl"

uniform mat4  u_model;
uniform float u_view_bias;

void main() {
  vec4 world = u_model * vec4(a_position, 1.0);
  vec4 view  = u_view * world;
  view.z += u_view_bias;
  gl_Position = u_proj * view;
  // Section clip. Inert unless the host enabled GL_CLIP_DISTANCE0 — see
  // u_clip_plane in common/frame_block.glsl. Measured on the UNBIASED world
  // position: u_view_bias is a depth-only nudge to keep a line in front of its
  // surface, and letting it move the clip test would trim edges at a slightly
  // different plane than the faces they sit on.
  //
  // This program doubles as the section cap's stencil-counting pass (it is the
  // only position-only program that already binds FrameBlock and takes
  // u_model), so this line is what makes that pass count CLIPPED geometry —
  // without it every closed solid balances and no cap is ever produced.
  gl_ClipDistance[0] = dot(u_clip_plane, world);
}

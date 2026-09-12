#version 410 core

// Section (剖切) pass vertex stage. One program serves every piece of section
// geometry — the stencil-masked cap fill, the translucent plane the gizmo shows,
// its border, and the drag handle — because they differ only in colour, hatch,
// and the model matrix. Which piece is being drawn is entirely a uniform
// decision on the fragment side.
//
// Deliberately does NOT write gl_ClipDistance[0]: section geometry must never be
// clipped by its own plane. The cap quad in particular sits exactly ON the plane
// where the distance is 0, and a floating-point comparison there is a coin flip.
// The host disables GL_CLIP_DISTANCE0 around these draws, so the value is
// ignored regardless — but keeping the write out documents the intent.

layout(location = 0) in vec3 a_position;

#include "common/frame_block.glsl"

uniform mat4 u_model;

// Plane basis, used to evaluate the hatch in plane space rather than in screen
// space. A screen-space hatch swims across the cut face while the camera orbits,
// which reads as the geometry moving; a plane-space hatch is locked to the cut,
// exactly like the hatch on a paper drawing.
uniform vec3 u_plane_origin;
uniform vec3 u_plane_u;
uniform vec3 u_plane_v;

out vec2 v_plane_uv;

void main() {
  vec4 world = u_model * vec4(a_position, 1.0);
  vec3 rel   = world.xyz - u_plane_origin;
  v_plane_uv = vec2(dot(rel, u_plane_u), dot(rel, u_plane_v));
  gl_Position = u_view_proj * world;
}

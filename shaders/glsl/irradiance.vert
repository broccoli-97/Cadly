#version 410 core

// Same vertex stage as env_capture: emit a world-space direction by
// projecting the fullscreen quad through a per-face (right, up, fwd) basis.
// Shared by every IBL bake pass that walks all six cube faces.

uniform mat3 u_face_basis;

#include "common/fullscreen_triangle.glsl"

out vec3 v_world_dir;

void main() {
  vec2 p = fullscreen_triangle_position();
  gl_Position = vec4(p, 1.0, 1.0);
  v_world_dir = u_face_basis[0] * p.x +
                u_face_basis[1] * p.y +
                u_face_basis[2];
}

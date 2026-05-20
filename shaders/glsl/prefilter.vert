#version 410 core

// Same vertex stage as env_capture/irradiance: project a fullscreen quad
// through a per-face basis to emit a world-space sample direction.

uniform mat3 u_face_basis;

out vec3 v_world_dir;

void main() {
  vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0,
                (gl_VertexID == 2) ? 3.0 : -1.0);
  gl_Position = vec4(p, 1.0, 1.0);
  v_world_dir = u_face_basis[0] * p.x +
                u_face_basis[1] * p.y +
                u_face_basis[2];
}

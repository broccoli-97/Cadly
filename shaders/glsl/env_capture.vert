#version 410 core

// Fullscreen-triangle vertex stage for rendering procedural sky into a single
// cubemap face. The host passes a `u_face_basis` mat3 whose columns are the
// (right, up, forward) world-space axes of the face being rendered; the
// fragment shader integrates this with the clipspace position to produce the
// sample direction. Using a basis matrix keeps the per-face setup tiny on the
// host side — six draws differ only by which basis we upload.

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

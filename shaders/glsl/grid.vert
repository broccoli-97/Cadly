#version 410 core

// Infinite-grid trick: draw a fullscreen quad on the ground plane and let the
// fragment shader compute the world position via ray/plane intersection.

layout(location = 0) in vec3 a_position;

uniform mat4 u_view_proj;
uniform mat4 u_inv_view_proj;

out vec3 v_near;
out vec3 v_far;

vec3 unproject(vec3 ndc) {
  vec4 p = u_inv_view_proj * vec4(ndc, 1.0);
  return p.xyz / p.w;
}

void main() {
  v_near = unproject(vec3(a_position.xy, -1.0));
  v_far  = unproject(vec3(a_position.xy,  1.0));
  gl_Position = vec4(a_position.xy, 0.0, 1.0);
}

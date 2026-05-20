#version 410 core

// BRep edge polyline pass. Only consumes positions — colour and bias are
// uniforms — so it reuses the host edge VBO directly. The edge is pulled
// slightly toward the camera in view space (`u_view_bias`) so it sits on
// top of the shaded surface without falling into per-pixel Z-fighting.
// Doing the bias in view space (rather than glPolygonOffset on the surface
// pass) keeps the offset perceptually constant across distances and play
// well with depth-tested transparency in future passes.

layout(location = 0) in vec3 a_position;

layout(std140) uniform FrameBlock {
  mat4 u_view;
  mat4 u_proj;
  mat4 u_view_proj;
  vec4 u_camera_pos;
  vec4 u_ambient;
  vec4 u_key_dir;
  vec4 u_key_color;
  vec4 u_fill_dir;
  vec4 u_fill_color;
  vec4 u_rim_dir;
  vec4 u_rim_color;
};

uniform mat4  u_model;
uniform float u_view_bias;

void main() {
  vec4 world = u_model * vec4(a_position, 1.0);
  vec4 view  = u_view * world;
  view.z += u_view_bias;
  gl_Position = u_proj * view;
}

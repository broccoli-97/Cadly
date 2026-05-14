#version 410 core

// Industrial-CAD PBR vertex stage. Keeps the model-matrix outside the UBO so
// each draw can push its own without touching uniform block memory.

layout(location = 0) in vec3  a_position;
layout(location = 1) in vec3  a_normal;
layout(location = 2) in vec4  a_color;       // unpacked from rgba8 by the host

layout(std140) uniform FrameBlock {
  mat4 u_view;
  mat4 u_proj;
  mat4 u_view_proj;
  vec4 u_camera_pos;     // .xyz position, .w = unused
  vec4 u_ambient;
  vec4 u_key_dir;
  vec4 u_key_color;
  vec4 u_fill_dir;
  vec4 u_fill_color;
  vec4 u_rim_dir;
  vec4 u_rim_color;
};

uniform mat4 u_model;
uniform mat3 u_normal_matrix;

out vec3 v_world_pos;
out vec3 v_world_normal;
out vec4 v_vertex_color;

void main() {
  vec4 world = u_model * vec4(a_position, 1.0);
  v_world_pos    = world.xyz;
  v_world_normal = normalize(u_normal_matrix * a_normal);
  v_vertex_color = a_color;
  gl_Position    = u_view_proj * world;
}

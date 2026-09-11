#version 410 core

// Industrial-CAD PBR vertex stage. Keeps the model-matrix outside the UBO so
// each draw can push its own without touching uniform block memory.

layout(location = 0) in vec3  a_position;
layout(location = 1) in vec3  a_normal;
layout(location = 2) in vec4  a_color;       // unpacked from rgba8 by the host

#include "common/frame_block.glsl"

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
  // Section clip. Inert unless the host enabled GL_CLIP_DISTANCE0 — see
  // u_clip_plane in common/frame_block.glsl.
  gl_ClipDistance[0] = dot(u_clip_plane, world);
}

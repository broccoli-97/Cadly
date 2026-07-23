#version 410 core

// Fullscreen background gradient — drawn first with depth write disabled.

out vec2 v_uv;

#include "common/fullscreen_triangle.glsl"

void main() {
  vec2 p = fullscreen_triangle_position();
  gl_Position = vec4(p, 1.0, 1.0);  // Far plane
  v_uv = p * 0.5 + 0.5;
}

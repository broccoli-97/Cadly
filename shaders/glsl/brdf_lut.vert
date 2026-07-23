#version 410 core

// Fullscreen triangle for the BRDF LUT bake. The LUT is a single 2D pass
// (no per-face iteration) so this stage is the standard "emit a giant
// triangle covering the screen and pass through UV" pattern.

out vec2 v_uv;

#include "common/fullscreen_triangle.glsl"

void main() {
  vec2 p = fullscreen_triangle_position();
  gl_Position = vec4(p, 1.0, 1.0);
  v_uv = p * 0.5 + 0.5;
}

#version 410 core

in  vec3 v_near;
in  vec3 v_far;

uniform mat4 u_view_proj;
uniform float u_scale;     // world units per major cell
uniform vec3 u_color_minor;
uniform vec3 u_color_major;
uniform vec3 u_color_axis_x;
uniform vec3 u_color_axis_z;
uniform float u_fade_start;
uniform float u_fade_end;

out vec4 frag_color;

float grid_intensity(vec2 p, float scale, float thickness) {
  vec2 coord = p / scale;
  vec2 dcoord = fwidth(coord);
  vec2 grid_lines = abs(fract(coord - 0.5) - 0.5) / max(dcoord, vec2(1e-5));
  float line = min(grid_lines.x, grid_lines.y);
  return 1.0 - smoothstep(thickness, thickness + 1.5, line);
}

void main() {
  // Ray/ground-plane intersection (y = 0).
  vec3 ray_dir = v_far - v_near;
  float t = -v_near.y / max(ray_dir.y, 1e-6);
  if (t < 0.0) discard;

  vec3 P = v_near + t * ray_dir;

  float minor = grid_intensity(P.xz, u_scale,        0.7);
  float major = grid_intensity(P.xz, u_scale * 10.0, 0.9);

  vec3 col = mix(u_color_minor * minor, u_color_major, max(major, 0.0) * 0.85);

  // Coloured axes.
  vec2 ax = abs(P.xz) / max(fwidth(P.xz), vec2(1e-5));
  float axis_x = 1.0 - smoothstep(0.7, 1.5, ax.y);
  float axis_z = 1.0 - smoothstep(0.7, 1.5, ax.x);
  col = mix(col, u_color_axis_x, axis_x * 0.85);
  col = mix(col, u_color_axis_z, axis_z * 0.85);

  // Distance fade so the grid doesn't dominate at the horizon.
  float dist = length(P.xz);
  float alpha = clamp(1.0 - smoothstep(u_fade_start, u_fade_end, dist), 0.0, 1.0);
  alpha *= max(minor, max(major, max(axis_x, axis_z)));
  if (alpha < 0.005) discard;

  // Reproject so the grid writes correct depth and integrates with the depth
  // prepass.
  vec4 clip = u_view_proj * vec4(P, 1.0);
  gl_FragDepth = clip.z / clip.w * 0.5 + 0.5;

  col = pow(col, vec3(1.0 / 2.2));
  frag_color = vec4(col, alpha);
}

#version 410 core

// Rotation-pivot indicator fragment shader. Paints a filled inner disc and an
// outer ring with antialiased edges. The sprite is screen-space-stable, so
// fwidth() gives ~1 pixel of feather regardless of camera distance.

in vec2 v_uv;

uniform vec4 u_fill_color;
uniform vec4 u_ring_color;

out vec4 frag_color;

void main() {
  float r = length(v_uv);
  float aa = fwidth(r) + 0.001;

  // Inner solid disc.
  float fill = 1.0 - smoothstep(0.40 - aa, 0.40 + aa, r);

  // Outer ring: difference of two circles.
  float outer = 1.0 - smoothstep(0.92 - aa, 0.92 + aa, r);
  float inner = 1.0 - smoothstep(0.66 - aa, 0.66 + aa, r);
  float ring  = clamp(outer - inner, 0.0, 1.0);

  vec3 col = u_fill_color.rgb * fill + u_ring_color.rgb * ring;
  float a  = max(fill * u_fill_color.a, ring * u_ring_color.a);
  if (a < 0.01) discard;

  frag_color = vec4(col, a);
}

#version 410 core

in  vec2 v_uv;
uniform vec3 u_top;
uniform vec3 u_bottom;

out vec4 frag_color;

void main() {
  // Smooth, gamma-correct vertical gradient. Stays muted so the model reads
  // as the focal subject.
  float t = smoothstep(0.0, 1.0, v_uv.y);
  vec3 col = mix(u_bottom, u_top, t);
  col = pow(col, vec3(1.0 / 2.2));
  frag_color = vec4(col, 1.0);
}

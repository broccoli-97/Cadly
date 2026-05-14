#version 410 core

// Cook-Torrance metallic-roughness with three analytical lights and a soft
// ambient term. Intentionally avoids HDRI IBL — that's a Milestone 4 task
// once prefiltered cubemaps are wired up. The result is bright, neutral, and
// readable, which is what mechanical inspection needs.

in vec3 v_world_pos;
in vec3 v_world_normal;
in vec4 v_vertex_color;

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

uniform vec4  u_base_color;          // sRGB linearised
uniform float u_metallic;
uniform float u_roughness;
uniform float u_reflectance;         // dielectric F0 control (0..1)
uniform vec3  u_emissive_color;
uniform float u_emissive;

out vec4 frag_color;

const float kPI = 3.14159265359;

float distribution_ggx(float NoH, float roughness) {
  float a  = roughness * roughness;
  float a2 = a * a;
  float NoH2 = NoH * NoH;
  float denom = NoH2 * (a2 - 1.0) + 1.0;
  return a2 / max(kPI * denom * denom, 1e-7);
}

float geometry_smith(float NoV, float NoL, float roughness) {
  float r = roughness + 1.0;
  float k = (r * r) / 8.0;
  float gv = NoV / (NoV * (1.0 - k) + k);
  float gl = NoL / (NoL * (1.0 - k) + k);
  return gv * gl;
}

vec3 fresnel_schlick(float HoV, vec3 F0) {
  return F0 + (1.0 - F0) * pow(clamp(1.0 - HoV, 0.0, 1.0), 5.0);
}

vec3 evaluate_light(vec3 N, vec3 V, vec3 L_dir, vec3 light_color,
                    vec3 base_color, float metallic, float roughness, vec3 F0) {
  vec3 L = normalize(-L_dir);
  vec3 H = normalize(L + V);

  float NoL = max(dot(N, L), 0.0);
  if (NoL <= 0.0) return vec3(0.0);

  float NoV = max(dot(N, V), 1e-4);
  float NoH = max(dot(N, H), 0.0);
  float HoV = max(dot(H, V), 0.0);

  float D = distribution_ggx(NoH, roughness);
  float G = geometry_smith(NoV, NoL, roughness);
  vec3  F = fresnel_schlick(HoV, F0);

  vec3 specular = (D * G * F) / max(4.0 * NoV * NoL, 1e-7);
  vec3 kd = (vec3(1.0) - F) * (1.0 - metallic);
  vec3 diffuse = kd * base_color / kPI;

  return (diffuse + specular) * light_color * NoL;
}

void main() {
  // Two-sided fallback so missing/back-faces still shade — common in CAD.
  vec3 N = normalize(v_world_normal);
  vec3 V = normalize(u_camera_pos.xyz - v_world_pos);
  if (dot(N, V) < 0.0) N = -N;

  vec3 base = u_base_color.rgb * v_vertex_color.rgb;
  float metallic  = clamp(u_metallic,  0.0, 1.0);
  float roughness = clamp(u_roughness, 0.045, 1.0);

  vec3 F0_dielectric = vec3(0.16 * u_reflectance * u_reflectance);
  vec3 F0 = mix(F0_dielectric, base, metallic);

  vec3 Lo = vec3(0.0);
  Lo += evaluate_light(N, V, u_key_dir.xyz,  u_key_color.rgb,  base, metallic, roughness, F0);
  Lo += evaluate_light(N, V, u_fill_dir.xyz, u_fill_color.rgb, base, metallic, roughness, F0);
  Lo += evaluate_light(N, V, u_rim_dir.xyz,  u_rim_color.rgb,  base, metallic, roughness, F0);

  // Hemispheric ambient lifts shadowed surfaces without flattening them.
  float upness = 0.5 + 0.5 * N.y;
  vec3 ambient = mix(u_ambient.rgb * 0.6, u_ambient.rgb * 1.4, upness) * base * (1.0 - metallic * 0.5);

  vec3 color = ambient + Lo + u_emissive_color * u_emissive;

  // Reinhard-ish tonemap + gamma for the swapchain (no sRGB framebuffer yet).
  color = color / (color + vec3(1.0));
  color = pow(color, vec3(1.0 / 2.2));

  frag_color = vec4(color, u_base_color.a);
}

#version 410 core

// Cook-Torrance metallic-roughness with three analytical lights and
// image-based lighting (split-sum / Karis 2013) for ambient + reflections.
// The IBL targets are baked once at renderer init from a procedural studio
// environment; the analytical lights stay on top for directional definition
// (IBL alone makes machined parts look soft).

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

uniform samplerCube u_irradiance_cube;  // diffuse IBL
uniform samplerCube u_prefilter_cube;   // specular IBL (mip chain by roughness)
uniform sampler2D   u_brdf_lut;         // split-sum BRDF LUT
uniform float       u_prefilter_max_lod;

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

vec3 fresnel_schlick_roughness(float HoV, vec3 F0, float roughness) {
  // Roughness-aware Schlick — used only for the IBL specular branch where
  // there's no half-vector to sample F at; replacing 1-F with this
  // (1-max(F0,1-rough)) gives the standard energy-conserving fall-off.
  return F0 + (max(vec3(1.0 - roughness), F0) - F0) *
              pow(clamp(1.0 - HoV, 0.0, 1.0), 5.0);
}

void main() {
  // Build a "shading normal" that never points below the visible horizon.
  //
  // On a curved surface, smooth interpolated per-vertex normals can dip
  // below dot(N,V)=0 along the silhouette of a front-facing triangle.
  // Flipping N there (the old fallback) inverts the reflection vector and
  // samples IBL from the opposite hemisphere -> back-side metallic smear.
  // Zeroing IBL there leaves the silhouette black -> kills the grazing-
  // angle Fresnel response that real materials show (chrome ball rim,
  // glass-marble edge brightening).
  //
  // The physically correct fix is to clamp N into the visible hemisphere:
  // push N just above the V-perpendicular plane while keeping it as close
  // as possible to its original direction. The resulting reflection vector
  // is near-tangent at the silhouette, which is what a grazing-angle pixel
  // actually reflects (the side environment), and Fresnel naturally drives
  // the rim toward pure reflection.
  //
  // Back-facing fragments only reach the shader on double-sided / non-
  // manifold geometry. There no clamp is meaningful (we're looking into
  // the surface), so fall back to the historical normal-flip so the
  // interior receives diffuse light at all.
  vec3 N_raw = normalize(v_world_normal);
  vec3 V     = normalize(u_camera_pos.xyz - v_world_pos);
  vec3 N;
  if (gl_FrontFacing) {
    float NoV_raw = dot(N_raw, V);
    N = (NoV_raw < 0.0)
      ? normalize(N_raw + V * (-NoV_raw + 1e-3))
      : N_raw;
  } else {
    N = -N_raw;
  }

  vec3 base = u_base_color.rgb * v_vertex_color.rgb;
  float metallic  = clamp(u_metallic,  0.0, 1.0);
  float roughness = clamp(u_roughness, 0.045, 1.0);

  vec3 F0_dielectric = vec3(0.16 * u_reflectance * u_reflectance);
  vec3 F0 = mix(F0_dielectric, base, metallic);

  vec3 Lo = vec3(0.0);
  Lo += evaluate_light(N, V, u_key_dir.xyz,  u_key_color.rgb,  base, metallic, roughness, F0);
  Lo += evaluate_light(N, V, u_fill_dir.xyz, u_fill_color.rgb, base, metallic, roughness, F0);
  Lo += evaluate_light(N, V, u_rim_dir.xyz,  u_rim_color.rgb,  base, metallic, roughness, F0);

  // IBL split-sum (Karis 2013). The diffuse term reads the cosine-weighted
  // irradiance cubemap; the specular term reads the GGX-prefiltered cubemap
  // at the mip level matching this surface's roughness, weighted by the
  // BRDF LUT that encodes the (NoV, roughness) -> (scale, bias) integral.
  // N is already hemisphere-clamped above, so NoV is guaranteed > 0 and
  // R stays on the visible side — no extra fade or fallback needed.
  float NoV = max(dot(N, V), 1e-4);
  vec3  F_ibl  = fresnel_schlick_roughness(NoV, F0, roughness);
  vec3  kS_ibl = F_ibl;
  vec3  kD_ibl = (vec3(1.0) - kS_ibl) * (1.0 - metallic);

  vec3 irradiance  = texture(u_irradiance_cube, N).rgb;
  vec3 diffuse_ibl = irradiance * base;

  vec3  R = reflect(-V, N);
  vec3  prefiltered = textureLod(u_prefilter_cube, R,
                                 roughness * u_prefilter_max_lod).rgb;
  vec2  brdf = texture(u_brdf_lut, vec2(NoV, roughness)).rg;
  vec3  specular_ibl = prefiltered * (F_ibl * brdf.x + brdf.y);

  // Mild fallback if the IBL textures weren't bound (zero samples). The
  // analytical `u_ambient` keeps the constant-ambient behaviour as a
  // floor — invisible whenever IBL is healthy, visible if the bake failed.
  vec3 ibl = kD_ibl * diffuse_ibl + specular_ibl;
  vec3 ambient_fallback = u_ambient.rgb * base * (1.0 - metallic * 0.5);
  vec3 ambient = ibl + ambient_fallback * 0.15;

  vec3 color = ambient + Lo + u_emissive_color * u_emissive;

  // Screen-space feature-edge enhancement. The face-on view of an embossed
  // CAD part hides relief by construction — the plate face and the raised
  // letter face share the same normal, while the side walls between them
  // project to ~0 pixels. The fragment derivatives of the world normal spike
  // exactly across those tiny strips (and across silhouettes against the
  // background), so we use them as an outline mask. Without this the user
  // sees a flat grey rectangle no matter how the lights are tuned.
  vec3 dNx = dFdx(N);
  vec3 dNy = dFdy(N);
  float edge = sqrt(dot(dNx, dNx) + dot(dNy, dNy));
  // Also tap depth derivative — catches silhouettes and crease folds where
  // the two surfaces happen to share a normal direction.
  float depth_edge = fwidth(gl_FragCoord.z) * 200.0;
  edge = clamp(max(edge * 2.5, depth_edge), 0.0, 1.0);
  // Pull edges toward black with a soft curve so the effect reads as
  // shading, not a hard cartoon outline.
  color *= mix(1.0, 0.55, edge * edge);

  // Soft-knee tonemap. Pure Reinhard (c/(c+1)) crushes the 0.5–1.5 range —
  // exactly where well-lit CAD surfaces land — and would erase the contrast
  // we just generated. Apply exposure-style compression only above 1.0 and
  // leave the low/mid range linear.
  color = color / (1.0 + 0.35 * max(color - vec3(0.4), vec3(0.0)));
  color = clamp(color, 0.0, 1.0);
  color = pow(color, vec3(1.0 / 2.2));

  frag_color = vec4(color, u_base_color.a);
}

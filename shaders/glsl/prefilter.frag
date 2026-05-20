#version 410 core

// Specular IBL prefilter — split-sum lighting approximation, half #1.
// For each mip level we treat the texel direction as both the surface normal
// and the view direction (R == V == N) and integrate the environment against
// a GGX lobe of the corresponding roughness using Hammersley + GGX
// importance sampling. The runtime PBR shader picks a mip with
// `roughness * (mip_count - 1)` to read the appropriately blurred specular.
//
// The R == V == N simplification (Karis 2013) drops grazing-angle specular
// stretch but is what makes the whole technique fit a 2D LUT for the BRDF
// term — accepted trade-off for real-time PBR.

in vec3 v_world_dir;
out vec4 frag_color;

uniform samplerCube u_env_cube;
uniform float u_roughness;
uniform float u_env_resolution;   // mip-0 face resolution in texels; used
                                  // to pick a source mip and limit aliasing

const float kPI = 3.14159265359;
const uint  kSampleCount = 1024u;

// Van der Corput radical inverse — low-discrepancy 1D sequence in [0,1).
float radical_inverse_vdc(uint bits) {
  bits = (bits << 16u) | (bits >> 16u);
  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
  return float(bits) * 2.3283064365386963e-10;
}

vec2 hammersley(uint i, uint n) {
  return vec2(float(i) / float(n), radical_inverse_vdc(i));
}

// GGX importance sample: map a Hammersley point to a half-vector H whose
// distribution matches the GGX NDF at the given roughness.
vec3 importance_sample_ggx(vec2 xi, vec3 N, float roughness) {
  float a = roughness * roughness;
  float phi      = 2.0 * kPI * xi.x;
  float cos_th   = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
  float sin_th   = sqrt(1.0 - cos_th * cos_th);

  vec3 H_tangent = vec3(sin_th * cos(phi), sin_th * sin(phi), cos_th);

  vec3 up    = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
  vec3 right = normalize(cross(up, N));
  up = cross(N, right);
  return normalize(H_tangent.x * right + H_tangent.y * up + H_tangent.z * N);
}

float distribution_ggx(float NoH, float roughness) {
  float a  = roughness * roughness;
  float a2 = a * a;
  float d  = (NoH * NoH) * (a2 - 1.0) + 1.0;
  return a2 / (kPI * d * d);
}

void main() {
  vec3 N = normalize(v_world_dir);
  vec3 R = N;
  vec3 V = R;

  vec3 prefiltered = vec3(0.0);
  float total_weight = 0.0;

  for (uint i = 0u; i < kSampleCount; ++i) {
    vec2 xi = hammersley(i, kSampleCount);
    vec3 H  = importance_sample_ggx(xi, N, u_roughness);
    vec3 L  = normalize(2.0 * dot(V, H) * H - V);

    float NoL = max(dot(N, L), 0.0);
    if (NoL > 0.0) {
      // Pick a source mip whose solid-angle-per-texel matches the
      // sample's PDF — kills the fireflies that plain mip 0 would give at
      // medium-to-high roughness.
      float NoH = max(dot(N, H), 0.0);
      float HoV = max(dot(H, V), 0.0);
      float D   = distribution_ggx(NoH, u_roughness);
      float pdf = (D * NoH / (4.0 * HoV)) + 1e-4;

      float sa_texel  = 4.0 * kPI / (6.0 * u_env_resolution * u_env_resolution);
      float sa_sample = 1.0 / (float(kSampleCount) * pdf + 1e-4);
      float mip = u_roughness == 0.0 ? 0.0
                : 0.5 * log2(sa_sample / sa_texel);

      prefiltered += textureLod(u_env_cube, L, mip).rgb * NoL;
      total_weight += NoL;
    }
  }
  prefiltered /= total_weight;
  frag_color = vec4(prefiltered, 1.0);
}

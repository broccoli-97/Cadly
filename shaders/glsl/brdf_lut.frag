#version 410 core

// Split-sum BRDF integration LUT — half #2 of the Karis split-sum technique.
// Output is a 2-channel 2D texture parameterised by (NoV, roughness), with
// R = scale and G = bias such that the runtime shader can reconstruct the
// specular IBL term as `prefiltered * (F0 * scale + bias)`.
//
// One bake at startup serves every material — the result is independent of
// F0, base colour, and environment, so we never need to recompute it.

in vec2 v_uv;
out vec2 frag_color;   // RG16F target

const float kPI = 3.14159265359;
const uint  kSampleCount = 1024u;

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

vec3 importance_sample_ggx(vec2 xi, vec3 N, float roughness) {
  float a = roughness * roughness;
  float phi    = 2.0 * kPI * xi.x;
  float cos_th = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
  float sin_th = sqrt(1.0 - cos_th * cos_th);

  vec3 H_tangent = vec3(sin_th * cos(phi), sin_th * sin(phi), cos_th);

  vec3 up    = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
  vec3 right = normalize(cross(up, N));
  up = cross(N, right);
  return normalize(H_tangent.x * right + H_tangent.y * up + H_tangent.z * N);
}

// Smith geometry term using the IBL-specific k = a^2 / 2 (NOT the analytical
// k = (a+1)^2/8 used in pbr.frag — they're different by design).
float geometry_schlick_ibl(float NoV, float roughness) {
  float a = roughness;
  float k = (a * a) / 2.0;
  return NoV / (NoV * (1.0 - k) + k);
}

float geometry_smith_ibl(vec3 N, vec3 V, vec3 L, float roughness) {
  float NoV = max(dot(N, V), 0.0);
  float NoL = max(dot(N, L), 0.0);
  return geometry_schlick_ibl(NoV, roughness) *
         geometry_schlick_ibl(NoL, roughness);
}

vec2 integrate_brdf(float NoV, float roughness) {
  vec3 V = vec3(sqrt(1.0 - NoV * NoV), 0.0, NoV);
  vec3 N = vec3(0.0, 0.0, 1.0);

  float A = 0.0;
  float B = 0.0;
  for (uint i = 0u; i < kSampleCount; ++i) {
    vec2 xi = hammersley(i, kSampleCount);
    vec3 H  = importance_sample_ggx(xi, N, roughness);
    vec3 L  = normalize(2.0 * dot(V, H) * H - V);

    float NoL = max(L.z, 0.0);
    float NoH = max(H.z, 0.0);
    float VoH = max(dot(V, H), 0.0);

    if (NoL > 0.0) {
      float G    = geometry_smith_ibl(N, V, L, roughness);
      float G_vis = (G * VoH) / (NoH * NoV);
      float Fc   = pow(1.0 - VoH, 5.0);
      A += (1.0 - Fc) * G_vis;
      B += Fc * G_vis;
    }
  }
  return vec2(A, B) / float(kSampleCount);
}

void main() {
  // Map [0,1] UV onto (NoV, roughness). Tiny epsilon on NoV avoids the
  // grazing-angle div-by-zero in the geometry term.
  float NoV       = max(v_uv.x, 1e-4);
  float roughness = v_uv.y;
  frag_color = integrate_brdf(NoV, roughness);
}

#pragma once

#include "cadly/scene/Math.h"

#include <string>

namespace cadly::scene {

// Industrial PBR metallic-roughness material. Fields match the GLSL uniform
// block exactly so the renderer can memcpy when uploading per-draw UBOs.
struct Material {
  std::string name;

  // Default: a neutral matte clay — mid-grey dielectric. This is the
  // industry-standard "unknown material" for CAD inspection: with no
  // metalness the base colour survives as diffuse under any environment, so
  // silhouettes, fillets and grooves stay readable instead of dissolving
  // into whatever the IBL probe reflects (a fully-metallic default has no
  // diffuse term at all and reads as a flat grey mirror in a low-frequency
  // studio environment). Matches Material::neutral_clay() so
  // default-constructing a Material in the renderer's fallback path
  // produces the same look as explicitly asking for the default preset.
  vec4  base_color{0.62f, 0.62f, 0.62f, 1.0f};    // linear RGBA
  float metallic   {0.0f};
  float roughness  {0.45f};
  float reflectance{0.5f};                        // dielectric F0 ~0.04
  float emissive   {0.0f};
  vec3  emissive_color{0.0f};

  bool double_sided{false};

  // Default preset: neutral matte clay for parts with no colour/material
  // information. See the field defaults above for the reasoning; keep both
  // in sync.
  static Material neutral_clay() {
    Material m; m.name = "neutral_clay";
    m.base_color = vec4(0.62f, 0.62f, 0.62f, 1.0f);
    m.metallic   = 0.0f;
    m.roughness  = 0.45f;
    return m;
  }
  // Brushed-aluminum-like finish — fully metallic, mid roughness so the
  // highlights read as soft "frosted" reflections rather than a mirror.
  // An explicit preset for parts known to be machined metal; NOT the
  // default, because metallic=1 kills the diffuse term entirely.
  static Material brushed_metal() {
    Material m; m.name = "brushed_metal";
    m.base_color = vec4(0.910f, 0.920f, 0.925f, 1.0f);
    m.metallic   = 1.0f;
    m.roughness  = 0.35f;
    return m;
  }
  static Material steel() {
    Material m; m.name = "steel";
    m.base_color = vec4(0.560f, 0.570f, 0.580f, 1.0f);
    m.metallic   = 1.0f;
    m.roughness  = 0.30f;
    return m;
  }
  static Material aluminum() {
    Material m; m.name = "aluminum";
    m.base_color = vec4(0.913f, 0.921f, 0.925f, 1.0f);
    m.metallic   = 1.0f;
    m.roughness  = 0.20f;
    return m;
  }
  static Material plastic() {
    Material m; m.name = "plastic";
    m.base_color = vec4(0.85f, 0.85f, 0.85f, 1.0f);
    m.metallic   = 0.0f;
    // Matte enough that residual surface facetting does not turn into
    // visible specular banding on the IBL highlights. CAD inspection
    // doesn't need a shiny default.
    m.roughness  = 0.75f;
    return m;
  }
  static Material rubber() {
    Material m; m.name = "rubber";
    m.base_color = vec4(0.07f, 0.07f, 0.07f, 1.0f);
    m.metallic   = 0.0f;
    m.roughness  = 0.85f;
    return m;
  }
  static Material painted_metal(const vec3& linear_rgb) {
    Material m; m.name = "painted_metal";
    m.base_color = vec4(linear_rgb, 1.0f);
    m.metallic   = 0.1f;
    m.roughness  = 0.45f;
    return m;
  }
};

} // namespace cadly::scene

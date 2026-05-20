#pragma once

#include "cadly/scene/Math.h"

namespace cadly::scene {

// Directional lights + ambient floor. The three analytical lights provide
// directional definition; image-based lighting (baked once at renderer init
// from a procedural studio environment) provides the ambient/reflections.
// The `ambient` field below is a tiny last-resort floor used only when the
// IBL bake fails — under normal operation it contributes ~15% weight and is
// imperceptible against the irradiance map.
//
// IMPORTANT — direction convention:
//
//   `*_direction` is the direction the light TRAVELS, expressed in
//   WORLD space (Y-up; see Camera::kWorldUp).
//
// Lights are NOT rotated with the camera. A camera-locked ("headlight") rig
// keeps L·N constant as the user orbits, which makes the model read as flat:
// the only thing that changes between viewpoints is the projected area of
// side walls, with no shading cue. World-fixed lights let the camera-relative
// incidence change as the user inspects, which is what produces a sense of
// relief and matches the standard "industrial inspection" rig used by
// FreeCAD, SolidWorks, etc.
//
// All three directions are deliberately well off any major axis so no face
// of an axis-aligned part ends up parallel to a light: face-on views always
// receive at least one raking key/fill, never a perpendicular flood.
struct LightEnvironment {
  // Key: warm dominant light from the upper-right-front quadrant of the
  // world. Source roughly at (+5, +6, +4); light travels down-left-back.
  // Y component dominates slightly so vertical-walled parts get good
  // top-to-bottom shading.
  vec3 key_direction  {-0.57f, -0.69f, -0.45f};
  vec3 key_color      { 1.15f,  1.05f,  0.95f};

  // Fill: dim cool light from the upper-left-back quadrant — opposite side
  // from key in X and Z, so any face the key cannot reach still picks up
  // some illumination instead of going black.
  vec3 fill_direction { 0.64f, -0.54f,  0.55f};
  vec3 fill_color     { 0.22f,  0.26f,  0.34f};

  // Rim: warm back light from above. Source roughly at (0, +4, -5); travels
  // down and forward. Catches silhouettes the off-axis key/fill miss.
  vec3 rim_direction  { 0.00f, -0.62f,  0.78f};
  vec3 rim_color      { 0.40f,  0.32f,  0.22f};

  // Last-resort ambient floor used only if the IBL bake failed. Under
  // normal operation the irradiance cubemap dominates this term and the
  // value is invisible.
  vec3 ambient        { 0.12f,  0.13f,  0.16f};

  // Background gradient (top/bottom) painted before the depth prepass.
  // Mirrors the renderer DisplayMode default — kept in sync so a scene
  // constructed without a DisplayMode override still looks right.
  vec3 background_top   { 0.42f, 0.44f, 0.48f};
  vec3 background_bottom{ 0.22f, 0.23f, 0.26f};
};

} // namespace cadly::scene

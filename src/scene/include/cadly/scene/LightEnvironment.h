#pragma once

#include "cadly/scene/Math.h"

namespace cadly::scene {

// Directional + IBL stub. Real IBL prefilter is a Milestone 4 task — for the
// MVP we use three analytical lights tuned for industrial inspection: a key,
// a cool fill, and a warm rim. Magnitudes carry intensity.
//
// IMPORTANT — direction convention:
//
//   `*_direction` is the direction the light TRAVELS, expressed in
//   CAMERA-LOCAL space (standard OpenGL: -Z forward, +Y up, +X right).
//
// The renderer multiplies each direction by the camera's world orientation
// when filling the per-frame UBO so the lighting rig follows the user as
// they orbit. This is the "headlight" / inspection-viewer pattern, and it
// means the model stays consistently lit no matter where the camera ends
// up. The shader still consumes world-space directions, so nothing in the
// fragment shader changes — only the host-side upload does the transform.
struct LightEnvironment {
  // Key: warm dominant light placed over the photographer's right shoulder
  // in camera-local space — light travels down-left-forward (toward the
  // subject from the upper-right-behind-camera position).
  vec3 key_direction  {-0.45f, -0.85f, -0.30f};
  vec3 key_color      { 1.10f,  1.00f,  0.90f};

  // Fill: dim cool light from the opposite side, lifts shadows.
  vec3 fill_direction { 0.50f,  0.10f,  0.65f};
  vec3 fill_color     { 0.30f,  0.34f,  0.42f};

  // Rim: warm back light for silhouette separation. Travels toward the
  // camera (negative Z) so it paints edges visible from the viewer's side.
  vec3 rim_direction  { 0.20f, -0.20f, -1.00f};
  vec3 rim_color      { 0.45f,  0.35f,  0.20f};

  // Constant ambient term — replaced by IBL once an HDRI is loaded.
  vec3 ambient        { 0.06f,  0.07f,  0.09f};

  // Background gradient (top/bottom) painted before the depth prepass.
  vec3 background_top   { 0.13f, 0.14f, 0.16f};
  vec3 background_bottom{ 0.05f, 0.05f, 0.06f};
};

} // namespace cadly::scene

#pragma once

#include "cadly/scene/Math.h"

namespace cadly::scene {

// Directional + IBL stub. Real IBL prefilter is a Milestone 4 task — for the
// MVP we use three analytical lights tuned for industrial inspection: a key,
// a cool fill, and a warm rim. The shader interprets `directions` in world
// space; magnitudes carry intensity.
struct LightEnvironment {
  // Three rim lights and a fill — works without HDRI maps for the MVP.
  vec3 key_direction  {-0.45f, -0.85f, -0.30f};
  vec3 key_color      { 1.10f,  1.00f,  0.90f};

  vec3 fill_direction { 0.50f,  0.10f,  0.65f};
  vec3 fill_color     { 0.30f,  0.34f,  0.42f};

  vec3 rim_direction  { 0.20f, -0.20f, -1.00f};
  vec3 rim_color      { 0.45f,  0.35f,  0.20f};

  // Constant ambient term — replaced by IBL once an HDRI is loaded.
  vec3 ambient        { 0.06f,  0.07f,  0.09f};

  // Background gradient (top/bottom) painted before the depth prepass.
  vec3 background_top   { 0.13f, 0.14f, 0.16f};
  vec3 background_bottom{ 0.05f, 0.05f, 0.06f};
};

} // namespace cadly::scene

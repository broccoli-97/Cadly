#version 410 core

// Procedural studio environment. Bakes once into a cubemap at startup so the
// expensive IBL convolution passes that follow have a real HDR signal to
// chew on. The look is a soft three-zone hemisphere (warm ground, neutral
// horizon, cool zenith) with two analytical "softbox" highlights to give
// IBL specular reflections something specific to catch — without those
// blobs metallic parts would look like featureless grey balls regardless of
// roughness.
//
// All output is in LINEAR HDR. The capture FBO is RGBA16F so values >1 are
// preserved for the prefilter pass to importance-sample.

in vec3 v_world_dir;
out vec4 frag_color;

vec3 procedural_sky(vec3 dir) {
  dir = normalize(dir);

  // Three-zone vertical gradient. Slight warmth at the bottom mimics a
  // bounced-light floor; cool zenith mimics open studio fill.
  float upness = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
  vec3 ground  = vec3(0.18, 0.16, 0.14);
  vec3 horizon = vec3(0.55, 0.55, 0.58);
  vec3 zenith  = vec3(0.85, 0.92, 1.05);
  vec3 sky = mix(ground, horizon, smoothstep(0.05, 0.50, upness));
  sky      = mix(sky,    zenith,  smoothstep(0.50, 1.00, upness));

  // Warm key softbox upper-right-front. The high exponent narrows the blob
  // so it shows up as a crisp specular highlight on smooth metals while
  // still contributing diffusely to the irradiance convolution.
  vec3 sun_dir = normalize(vec3( 0.60,  0.70,  0.40));
  float sun = pow(max(dot(dir, sun_dir), 0.0), 14.0);
  sky += vec3(3.5, 3.0, 2.4) * sun;

  // Cool fill softbox upper-left-back, lower intensity and wider so it
  // wraps around the model rather than punching a hot spot.
  vec3 fill_dir = normalize(vec3(-0.55,  0.45, -0.40));
  float fill = pow(max(dot(dir, fill_dir), 0.0), 6.0);
  sky += vec3(0.55, 0.65, 0.85) * fill;

  return sky;
}

void main() {
  frag_color = vec4(procedural_sky(v_world_dir), 1.0);
}

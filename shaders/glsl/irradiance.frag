#version 410 core

// Diffuse irradiance convolution. For each output direction N we integrate
// the incoming radiance over the hemisphere weighted by Lambertian cosine,
// using a uniform Riemann sum on (phi, theta). The result is the precomputed
// integral that the runtime PBR shader divides by pi to obtain Lambertian
// diffuse from the environment.
//
// Target resolution is small (32x32 per face) because the convolved signal
// has no detail finer than a Lambertian lobe. Sampling stride 0.025 rad
// gives ~25k samples per output texel and is fast enough to bake at startup.

in vec3 v_world_dir;
out vec4 frag_color;

uniform samplerCube u_env_cube;

const float kPI = 3.14159265359;

void main() {
  vec3 N = normalize(v_world_dir);

  // Build an orthonormal basis around N so we can spin (phi, theta) in
  // tangent space and rotate samples into world space. The "up" reference
  // flips when N is nearly vertical to keep the cross product non-degenerate.
  vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
  vec3 right = normalize(cross(up, N));
  up = cross(N, right);

  vec3 irradiance = vec3(0.0);
  float sample_count = 0.0;
  const float delta = 0.025;
  for (float phi = 0.0; phi < 2.0 * kPI; phi += delta) {
    for (float theta = 0.0; theta < 0.5 * kPI; theta += delta) {
      float st = sin(theta);
      // Tangent-space sample direction, then rotate into world space.
      vec3 tangent = vec3(st * cos(phi), st * sin(phi), cos(theta));
      vec3 world   = tangent.x * right + tangent.y * up + tangent.z * N;
      irradiance  += texture(u_env_cube, world).rgb * cos(theta) * st;
      sample_count += 1.0;
    }
  }
  irradiance = kPI * irradiance / sample_count;
  frag_color = vec4(irradiance, 1.0);
}

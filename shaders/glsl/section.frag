#version 410 core

// Section (剖切) pass fragment stage: a flat colour with an optional drafting
// hatch. Used for the cut-face cap, the translucent plane preview, its border,
// and the drag handle.
//
// Colour space: this pass writes straight to the framebuffer with no tonemap and
// no gamma encode, so `u_color` must arrive already in framebuffer (post-gamma)
// space — the same contract draw_pivot and the 2D overlay pass work under, and
// the same one DisplayMode::selection_color documents. The host is responsible
// for matching the surface passes: in hidden-line mode the surrounding paper is
// pow(hidden_line_color, 1/2.2) (see pbr.frag), so the host encodes the cap
// colour identically or the cap shows as a patch of slightly-off grey.

in vec2 v_plane_uv;

uniform vec4  u_color;
// Hatch stripe spacing in world units. <= 0 disables the hatch entirely, which
// is how the plane preview, the border, and the handle share this program.
uniform float u_hatch_pitch;
uniform vec4  u_hatch_color;

out vec4 frag_color;

void main() {
  vec4 col = u_color;

  if (u_hatch_pitch > 0.0) {
    // 45-degree stripes: the sum of the two plane-space axes advances one full
    // period per `pitch` along the diagonal. Distance to the nearest stripe
    // centre, in periods, gives a signal fwidth() can antialias — without it a
    // fine hatch aliases into moire the moment the camera moves.
    float t     = (v_plane_uv.x + v_plane_uv.y) / u_hatch_pitch;
    float tri   = abs(fract(t) - 0.5);      // 0 at stripe centre, 0.5 between
    float footprint = fwidth(t);
    float aa    = 0.5 * footprint + 1e-5;
    const float half_w = 0.065;
    float line  = 1.0 - smoothstep(half_w - aa, half_w + aa, tri);
    // At grazing angles, converge to the stripe's average coverage instead of
    // aliasing or darkening the entire cap. Hatch changes RGB only, never alpha.
    line = mix(line, 2.0 * half_w, smoothstep(0.35, 0.75, footprint));
    col.rgb = mix(col.rgb, u_hatch_color.rgb, line * u_hatch_color.a);
  }

  if (col.a < 0.004) discard;
  frag_color = col;
}

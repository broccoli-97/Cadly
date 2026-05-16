#version 410 core

// Rotation-pivot indicator: a screen-space-stable sprite anchored to a world
// position. Two triangles forming a quad are emitted from gl_VertexID (no VBO
// needed); each corner UV is forwarded to the fragment shader, which paints
// the disc/ring.

uniform mat4 u_view_proj;
uniform vec3 u_world_pos;
uniform float u_size_px;     // sprite diameter in framebuffer pixels
uniform vec2 u_viewport_px;  // framebuffer extent in pixels

out vec2 v_uv;

void main() {
  // Corner offsets in [-1, 1] for two CCW triangles covering a unit quad.
  vec2 corners[6] = vec2[6](
    vec2(-1.0, -1.0), vec2( 1.0, -1.0), vec2( 1.0,  1.0),
    vec2(-1.0, -1.0), vec2( 1.0,  1.0), vec2(-1.0,  1.0)
  );
  vec2 corner = corners[gl_VertexID];
  v_uv = corner;

  vec4 clip = u_view_proj * vec4(u_world_pos, 1.0);
  if (clip.w <= 0.0) {
    // Pivot is behind the camera — kick the vertex off-screen so nothing draws.
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    return;
  }

  // NDC range is [-1, 1] (width 2), so a pixel in NDC is 2 / viewport_px.
  // The clip-space offset is scaled by clip.w so it survives the perspective
  // divide and yields the requested pixel diameter on screen.
  vec2 ndc_offset = corner * (u_size_px / u_viewport_px);
  clip.xy += ndc_offset * clip.w;
  gl_Position = clip;
}

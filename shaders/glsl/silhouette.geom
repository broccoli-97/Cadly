#version 410 core

// View-dependent silhouette extraction — the contour a technical drawing
// draws where a smooth surface curves away from the viewer (the sides of a
// cylinder, the outline of a sphere, the horizon arc of a fillet). These
// curves are not BRep edges, so the edge passes can never produce them; a
// rotated cylinder in hidden-line mode used to render as two floating
// ellipses with nothing in between. Commercial CAD (Creo, SolidWorks, NX)
// draws them as "silhouette edges" in every line-based display style.
//
// Method: the silhouette is the zero set of the facing function
//     d(p) = dot(N(p), toward_eye(p))
// over the surface. OCCT gives us exact surface normals at the mesh nodes
// (smooth within each face), so evaluating d at the three corners and
// linearly interpolating the d = 0 crossing INSIDE the triangle recovers the
// contour with sub-facet accuracy — far smoother and more stable under
// rotation than tagging whole mesh edges front/back, and it needs no
// adjacency buffer: each triangle is classified independently, which also
// makes seams (duplicated nodes with equal normals, e.g. a cylinder's
// parametric seam) come out watertight for free.
//
// The emitted segment endpoints start as barycentric points on the triangle
// and are reconstructed onto the smooth surface (see surface_crossing).
// Hidden-line mode additionally shifts the projected line outward by a fixed
// sub-pixel amount. Keeping that coverage adjustment in screen space is
// important: moving farther in world space makes long, diagonally split
// cylinders visibly bulge as the contour crosses each triangle.

layout(triangles) in;
layout(line_strip, max_vertices = 2) out;

in vec3 v_world_pos[];
in vec3 v_world_normal[];

#include "common/frame_block.glsl"

uniform vec2 u_viewport_px;
uniform float u_outward_px;

// The d = 0 crossing on edge a->b, lifted from the chord onto the smooth
// surface it approximates.
//
// The naive mix(p_a, p_b, t) point lies on the CHORD, which sags inside a
// convex surface by up to the mesher's linear deflection — i.e. the raw
// crossing is buried up to one sag INSIDE the solid. Right at the contour
// the view rays are tangent to the surface, so a world-space burial of one
// sag becomes a huge depth gap along the grazing ray, and whichever face
// rasterises in front there (the adjacent front-facing facet column, or a
// planar cap that meets the curved face at the rim) occludes the line by far
// more than glPolygonOffset compensates. The failure is quantised by the
// tessellation: on a cylinder the facing function is constant along each
// generator, so as the camera's azimuth sweeps across a facet the ENTIRE
// contour line pops in and out of visibility at once.
//
// Second-order reconstruction from the discrete normal curvature along this
// particular edge:
//     sag(t) = 0.5 * dot(n_b - n_a, p_b - p_a) * t * (1 - t).
// The dot product is the essential part. The previous implementation used
// 0.5*|delta-normal|*|chord|, its Cauchy upper bound, which is equal only when
// the edge follows one curvature direction. A cylinder's mesh diagonals also
// span its zero-curvature axial direction, so that bound treated the full
// cylinder length as curved and pushed the mid-height contour far outside the
// body. The directional form automatically discards that axial component and
// retains the signed convex/concave bend needed for outer walls and bores.
vec3 surface_crossing(vec3 p_a, vec3 p_b, vec3 n_a, vec3 n_b,
                      float d_a, float d_b, out vec3 n_c) {
  float t = d_a / (d_a - d_b);
  vec3 p = mix(p_a, p_b, t);
  n_c = mix(n_a, n_b, t);
  if (dot(n_c, n_c) < 1e-12) {
    n_c = n_a;
    return p;   // opposed normals — no local surface to reconstruct
  }
  n_c = normalize(n_c);
  vec3 chord = p_b - p_a;
  float sag = 0.5 * dot(n_b - n_a, chord) * t * (1.0 - t);
  return p + n_c * sag;
}

// Move a hidden-line contour just beyond the filled surface in screen space.
// At a smooth silhouette the projected normal points away from the body. A
// fixed pixel offset gives stable raster coverage at every zoom level without
// changing the model-space curve or pulling it through unrelated geometry in
// depth. Wireframe passes u_outward_px=0 because it has no filled surface to
// compete with.
vec4 project_contour(vec3 p, vec3 n) {
  vec4 clip = u_view_proj * vec4(p, 1.0);
  if (u_outward_px <= 0.0 || abs(clip.w) < 1e-8) return clip;

  // Derivative of the perspective divide for a displacement along n.
  vec4 delta_clip = u_view_proj * vec4(n, 0.0);
  vec2 ndc_dir = (delta_clip.xy * clip.w - clip.xy * delta_clip.w) /
                 (clip.w * clip.w);
  vec2 pixel_dir = ndc_dir * (0.5 * u_viewport_px);
  float pixel_len2 = dot(pixel_dir, pixel_dir);
  if (pixel_len2 < 1e-12) return clip;

  vec2 unit_pixel_dir = pixel_dir * inversesqrt(pixel_len2);
  vec2 ndc_offset = unit_pixel_dir * (2.0 * u_outward_px / u_viewport_px);
  clip.xy += ndc_offset * clip.w;
  return clip;
}

void main() {
  const float kFlatDot = 0.99999; // ~0.26 deg — see planar-facet cull below

  vec3 n0 = v_world_normal[0];
  vec3 n1 = v_world_normal[1];
  vec3 n2 = v_world_normal[2];

  // Degenerate normals (meshes imported without normals and with
  // compute_missing_normals off) make d identically zero; bail before the
  // normalize below turns them into NaNs.
  if (dot(n0, n0) < 1e-20 || dot(n1, n1) < 1e-20 || dot(n2, n2) < 1e-20) {
    return;
  }
  n0 = normalize(n0);
  n1 = normalize(n1);
  n2 = normalize(n2);

  // Planar facets can't carry a smooth-surface contour — a plane's outline
  // IS its BRep boundary, which the edge pass already draws. Culling them
  // here also kills the one false positive of the perspective facing
  // function: on a large flat face seen nearly edge-on, d flips sign purely
  // through the position term and would paint a stray "grazing line" across
  // the face. OCCT emits bit-identical normals across a planar face, so a
  // tight threshold cleanly separates flat from curved (default angular
  // deflection keeps curved-facet normal spread orders of magnitude above
  // 0.26 deg).
  if (dot(n0, n1) > kFlatDot && dot(n1, n2) > kFlatDot &&
      dot(n0, n2) > kFlatDot) {
    return;
  }

  float d0, d1, d2;
  if (u_view_ref.w > 0.5) {
    d0 = dot(n0, u_view_ref.xyz - v_world_pos[0]);
    d1 = dot(n1, u_view_ref.xyz - v_world_pos[1]);
    d2 = dot(n2, u_view_ref.xyz - v_world_pos[2]);
  } else {
    d0 = dot(n0, u_view_ref.xyz);
    d1 = dot(n1, u_view_ref.xyz);
    d2 = dot(n2, u_view_ref.xyz);
  }

  // Classify corners front/back. The >= 0 split makes the d == 0 knife-edge
  // case land in one bucket, so a proper sign change always yields exactly
  // two crossing edges and the divisions below can never see a zero
  // denominator.
  bool f0 = d0 >= 0.0;
  bool f1 = d1 >= 0.0;
  bool f2 = d2 >= 0.0;
  if (f0 == f1 && f1 == f2) return;   // wholly front- or back-facing

  vec3 pts[2];
  vec3 ns[2];
  int  count = 0;
  if (f0 != f1) {
    pts[count] = surface_crossing(v_world_pos[0], v_world_pos[1],
                                  n0, n1, d0, d1, ns[count]);
    ++count;
  }
  if (f1 != f2) {
    pts[count] = surface_crossing(v_world_pos[1], v_world_pos[2],
                                  n1, n2, d1, d2, ns[count]);
    ++count;
  }
  if (f2 != f0 && count < 2) {
    pts[count] = surface_crossing(v_world_pos[2], v_world_pos[0],
                                  n2, n0, d2, d0, ns[count]);
    ++count;
  }
  if (count < 2) return;

  gl_Position = project_contour(pts[0], ns[0]);
  // Section clip. User clipping runs after the LAST vertex-processing stage, so
  // for this program that is the geometry shader — writing gl_ClipDistance in
  // silhouette.vert would have no effect. It also has to be re-set before each
  // EmitVertex(), because every output variable becomes undefined after one.
  // Inert unless the host enabled GL_CLIP_DISTANCE0.
  //
  // The GS still receives whole, unclipped triangles, so the crossing search and
  // the surface reconstruction above are unaffected; only the emitted line gets
  // trimmed at the plane.
  gl_ClipDistance[0] = dot(u_clip_plane, vec4(pts[0], 1.0));
  EmitVertex();
  gl_Position = project_contour(pts[1], ns[1]);
  gl_ClipDistance[0] = dot(u_clip_plane, vec4(pts[1], 1.0));
  EmitVertex();
  EndPrimitive();
}

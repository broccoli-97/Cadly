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
// The emitted segment endpoints are barycentric points ON the triangle, so
// their depth is the surface's own depth: the renderer's polygon-offset
// scheme (faces pushed back, lines at true depth) keeps visible contours in
// front of their surface while genuinely occluded contours still lose the
// depth test — exactly the hidden-line semantics the BRep edge overlay uses.

layout(triangles) in;
layout(line_strip, max_vertices = 2) out;

in vec3 v_world_pos[];
in vec3 v_world_normal[];

layout(std140) uniform FrameBlock {
  mat4 u_view;
  mat4 u_proj;
  mat4 u_view_proj;
  vec4 u_camera_pos;
  vec4 u_ambient;
  vec4 u_key_dir;
  vec4 u_key_color;
  vec4 u_fill_dir;
  vec4 u_fill_color;
  vec4 u_rim_dir;
  vec4 u_rim_color;
};

// Perspective: .xyz = eye position, .w = 1 (toward-eye varies per vertex).
// Orthographic: .xyz = unit vector from surface toward the eye (-camera
// forward), .w = 0. Kept separate from FrameBlock's u_camera_pos because the
// facing function must match the PROJECTION, not the eye point: under ortho
// all view rays are parallel, and using the eye point instead would bow the
// contour of a long cylinder toward the eye's perpendicular foot.
uniform vec4 u_view_ref;

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
  int  count = 0;
  if (f0 != f1) {
    pts[count++] = mix(v_world_pos[0], v_world_pos[1], d0 / (d0 - d1));
  }
  if (f1 != f2) {
    pts[count++] = mix(v_world_pos[1], v_world_pos[2], d1 / (d1 - d2));
  }
  if (f2 != f0 && count < 2) {
    pts[count++] = mix(v_world_pos[2], v_world_pos[0], d2 / (d2 - d0));
  }
  if (count < 2) return;

  gl_Position = u_view_proj * vec4(pts[0], 1.0);
  EmitVertex();
  gl_Position = u_view_proj * vec4(pts[1], 1.0);
  EmitVertex();
  EndPrimitive();
}

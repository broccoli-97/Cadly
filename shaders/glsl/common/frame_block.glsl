layout(std140) uniform FrameBlock {
  mat4 u_view;
  mat4 u_proj;
  mat4 u_view_proj;
  // Perspective: xyz is the eye position and w is 1. Orthographic: xyz is
  // the constant unit direction from the surface toward the viewer and w is
  // 0. Shading and view-dependent geometry must honor this distinction.
  vec4 u_view_ref;
  vec4 u_ambient;
  vec4 u_key_dir;
  vec4 u_key_color;
  vec4 u_fill_dir;
  vec4 u_fill_color;
  vec4 u_rim_dir;
  vec4 u_rim_color;
};

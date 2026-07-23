vec2 fullscreen_triangle_position() {
  return vec2((gl_VertexID == 1) ? 3.0 : -1.0,
              (gl_VertexID == 2) ? 3.0 : -1.0);
}

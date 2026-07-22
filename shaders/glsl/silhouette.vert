#version 410 core

// Silhouette-extraction vertex stage. Feeds world-space position + normal to
// the geometry stage, which finds the view-dependent contour (the zero set of
// dot(N, toward-eye) over the surface) and emits it as line segments. Reads
// the same VAO layout as the PBR surface pass (locations 0/1; the rgba8
// colour at location 2 is simply not consumed), so the pass reuses the
// surface VBO/IBO with no extra geometry upload.
//
// gl_Position is deliberately not written: the geometry shader emits its own
// clip-space positions for the interpolated crossing points and never reads
// the provoking vertices' positions in clip space.

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;

uniform mat4 u_model;
uniform mat3 u_normal_matrix;

out vec3 v_world_pos;
out vec3 v_world_normal;

void main() {
  v_world_pos    = (u_model * vec4(a_position, 1.0)).xyz;
  v_world_normal = u_normal_matrix * a_normal;
}

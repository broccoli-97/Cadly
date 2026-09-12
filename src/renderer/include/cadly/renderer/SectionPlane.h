#pragma once

#include "cadly/scene/Aabb.h"
#include "cadly/scene/Math.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace cadly::renderer {

// The section (剖切) plane: one half-space that cuts the model open so internal
// features can be inspected.
//
// Why this lives in the `renderer` interface module rather than in `scene`:
// both the GL backend (which draws the clip, the cap, and the gizmo) and the Qt
// host (which hit-tests the drag handle and converts a mouse drag into a new
// offset) need the IDENTICAL derivation from plane + scene bounds. Duplicating
// it would let the visual plane and the draggable handle drift apart by a pixel
// or two, which reads as a broken gizmo. `scene` is the canonical model and
// should not carry view state (see docs/research/04-code-architecture-review.md),
// and both `renderer_gl` and `ui` already link Cadly::Renderer — so this header
// is the one place both sides can share.
//
// Convention: the plane keeps the half-space where `dot(equation, vec4(p, 1)) >= 0`.
// With `normal` pointing along the camera's forward direction, that keeps the
// FAR half and cuts away the near half — i.e. "open the model toward me", which
// is what every commercial section view does when you first switch it on.
struct SectionPlane {
  // Unit normal. The kept half-space lies on the +normal side.
  scene::vec3 normal{1.0f, 0.0f, 0.0f};

  // Signed distance along `normal` from the scene bounds CENTRE, not from the
  // world origin. Storing it relative to the model means the default (0) always
  // cuts through the middle of whatever was imported, the UI slider gets a
  // natural symmetric range, and a re-import at different absolute coordinates
  // does not leave the plane parked outside the part.
  float offset{0.0f};

  // Ax + By + Cz + D, with the kept material on the >= 0 side.
  scene::vec4 equation(const scene::Aabb& bounds) const {
    const scene::vec3 n = unit_normal();
    const scene::vec3 p = plane_point(bounds, n);
    return scene::vec4(n, -glm::dot(n, p));
  }

  // World-space origin for the gizmo: the bounds centre slid along the normal
  // by `offset`. The visible quad and the drag handle both hang off this.
  scene::vec3 anchor(const scene::Aabb& bounds) const {
    return plane_point(bounds, unit_normal());
  }

  // Two orthonormal in-plane axes. Only the plane matters, not which rotation
  // of it we pick, so this just avoids the degenerate cross product.
  void basis(scene::vec3& u, scene::vec3& v) const {
    const scene::vec3 n = unit_normal();
    // Cross with whichever cardinal axis the normal is least aligned to; that
    // guarantees a well-conditioned cross product for every possible normal.
    const scene::vec3 seed =
      (std::abs(n.x) < 0.9f) ? scene::vec3(1.0f, 0.0f, 0.0f)
                             : scene::vec3(0.0f, 1.0f, 0.0f);
    u = glm::normalize(glm::cross(seed, n));
    v = glm::cross(n, u);
  }

  // Half-edge of the square the cross-section polygon is clipped out of. Only
  // needs to be big enough to span the box from any anchor, hence the diagonal.
  float seed_half_size(const scene::Aabb& bounds) const {
    if (!bounds.valid()) return 1.0f;
    return std::max(glm::length(bounds.extent()), 1e-4f);
  }

  // Drag clamp: the AABB half-extent projected on the normal. At -range the
  // plane has just cleared the near side and the whole model survives; at
  // +range it has passed the far side and the whole model is cut away. Exactly
  // the useful travel, no more.
  float offset_range(const scene::Aabb& bounds) const {
    if (!bounds.valid()) return 1.0f;
    const scene::vec3 n = unit_normal();
    return std::max(glm::dot(glm::abs(n), 0.5f * bounds.extent()), 1e-4f);
  }

  // Normalised `normal`, with a fallback for a degenerate (zero) value so no
  // caller can produce a NaN plane equation.
  scene::vec3 unit_normal() const {
    const float len2 = glm::dot(normal, normal);
    if (len2 < 1e-12f) return scene::vec3(1.0f, 0.0f, 0.0f);
    return normal * (1.0f / std::sqrt(len2));
  }

private:
  scene::vec3 plane_point(const scene::Aabb& bounds,
                          const scene::vec3& n) const {
    const scene::vec3 centre =
      bounds.valid() ? bounds.center() : scene::vec3(0.0f);
    return centre + n * offset;
  }
};

// True when `box` has material on both sides of `plane` (Ax+By+Cz+D form, as
// produced by SectionPlane::equation).
//
// This is the cull that makes the stencil cap pass cheap. A node entirely on the
// kept side contributes a matched front/back pair to the counting pass and
// therefore a net zero; a node entirely on the cut side contributes no fragments
// at all. So only straddling nodes can change the mask, and the pass can skip
// everything else — on a large assembly that is a handful of parts instead of
// all of them.
//
// A free function taking the already-resolved equation, so the per-node loop
// resolves the plane once and SectionPlane stays a plain copyable value inside
// DisplayMode.
inline bool box_straddles_plane(const scene::Aabb& box,
                                const scene::vec4& plane) {
  if (!box.valid()) return false;
  const scene::vec3 n(plane);
  // Signed distance of the box centre, and the box's extent measured along the
  // normal (the standard AABB-vs-plane separating-axis radius).
  const float centre_d = glm::dot(n, box.center()) + plane.w;
  const float radius   = glm::dot(glm::abs(n), 0.5f * box.extent());
  return std::abs(centre_d) < radius;
}

// The convex polygon where the plane crosses `bounds`, wound consistently and
// ready to draw as a triangle fan. Returns the vertex count (0 when the plane
// misses the box), writing into `out`.
//
// This one polygon serves both the stencil-masked cap fill and the translucent
// plane the gizmo shows, and using it instead of a fixed square matters for
// three separate reasons:
//
//   1. Depth clipping. CameraController fits near/far to the scene's bounding
//      SPHERE, so a square of half-size r has corners at r*sqrt(2) that poke
//      out of the depth slab at grazing plane orientations — the cap would come
//      back with notches bitten out of its corners. Every vertex of this
//      polygon is inside the AABB by construction, hence inside the slab.
//   2. Coverage. Material is a subset of the bounds, so a polygon spanning the
//      bounds' cross-section is guaranteed to cover every pixel the stencil can
//      mark. No margin fudge factor to get wrong.
//   3. It is the right visual. A translucent plane sized to the model's actual
//      cross-section frames the part; an arbitrary square either floats past the
//      edges of the screen or falls short of the geometry.
//
// Method: seed a square on the plane large enough to span the box, then
// Sutherland-Hodgman it against the box's six half-spaces. `extra_offset` slides
// the polygon along the normal — the cap pass uses a tiny positive value so a
// model face lying exactly ON the section plane stays strictly in front of the
// cap and wins the depth test outright instead of Z-fighting it.
inline int plane_box_cross_section(const SectionPlane& plane,
                                  const scene::Aabb& bounds,
                                  float extra_offset,
                                  scene::vec3* out,
                                  int out_capacity) {
  // A quad clipped by six half-spaces can reach 10 vertices before degenerate
  // ones are dropped, even though a box cross-section is geometrically a
  // triangle through a hexagon.
  constexpr int kMaxVerts = 12;
  if (!bounds.valid() || out_capacity < kMaxVerts) return 0;

  const scene::vec3 n = plane.unit_normal();
  scene::vec3 u, v;
  plane.basis(u, v);
  const scene::vec3 centre = bounds.center() + n * (plane.offset + extra_offset);
  const float h = plane.seed_half_size(bounds);

  scene::vec3 buf_a[kMaxVerts];
  scene::vec3 buf_b[kMaxVerts];
  scene::vec3* poly = buf_a;
  scene::vec3* next = buf_b;
  int count = 4;
  poly[0] = centre - u * h - v * h;
  poly[1] = centre + u * h - v * h;
  poly[2] = centre + u * h + v * h;
  poly[3] = centre - u * h + v * h;

  // Six half-spaces, each `dot(axis, p) <= limit` with axis = +/- a cardinal.
  const scene::vec3 axes[6] = {
    { 1.0f,  0.0f,  0.0f}, {-1.0f,  0.0f,  0.0f},
    { 0.0f,  1.0f,  0.0f}, { 0.0f, -1.0f,  0.0f},
    { 0.0f,  0.0f,  1.0f}, { 0.0f,  0.0f, -1.0f},
  };
  const float limits[6] = {
     bounds.max.x, -bounds.min.x,
     bounds.max.y, -bounds.min.y,
     bounds.max.z, -bounds.min.z,
  };

  for (int face = 0; face < 6 && count > 0; ++face) {
    int written = 0;
    for (int i = 0; i < count; ++i) {
      const scene::vec3& cur = poly[i];
      const scene::vec3& prv = poly[(i + count - 1) % count];
      const float d_cur = glm::dot(axes[face], cur) - limits[face];
      const float d_prv = glm::dot(axes[face], prv) - limits[face];
      // Crossing the boundary: emit the intersection first, keeping the winding.
      if ((d_prv > 0.0f) != (d_cur > 0.0f)) {
        const float denom = d_prv - d_cur;
        if (std::abs(denom) > 1e-20f && written < kMaxVerts) {
          next[written] = prv + (cur - prv) * (d_prv / denom);
          ++written;
        }
      }
      if (d_cur <= 0.0f && written < kMaxVerts) {
        next[written] = cur;
        ++written;
      }
    }
    count = written;
    std::swap(poly, next);
  }

  if (count < 3) return 0;
  for (int i = 0; i < count; ++i) out[i] = poly[i];
  return count;
}

// Closest point along an infinite axis line to a screen ray, expressed as the
// signed distance `out_t` from `axis_origin` along `axis_dir`.
//
// This is the whole of axis-drag gizmo math: the handle can only slide along
// one line, so a drag is "where along that line is the cursor pointing". The
// standard line/line closest-approach solution degenerates when the axis points
// at the camera (the projected handle collapses to a point and the mapping from
// pixels to offset blows up), so callers get a false and should leave the offset
// alone for that frame rather than letting it jump.
inline bool closest_point_on_axis(const scene::vec3& axis_origin,
                                  const scene::vec3& axis_dir,
                                  const scene::vec3& ray_origin,
                                  const scene::vec3& ray_dir,
                                  float& out_t) {
  const scene::vec3 a = glm::normalize(axis_dir);
  const scene::vec3 b = glm::normalize(ray_dir);
  const float ab = glm::dot(a, b);
  // |ab| -> 1 means the axis is parallel to the view ray. 0.999 is about 2.6
  // degrees of separation, which is where a one-pixel cursor move already maps
  // to an unusable world jump.
  const float denom = 1.0f - ab * ab;
  if (denom < 1e-3f) return false;
  const scene::vec3 w = ray_origin - axis_origin;
  out_t = (glm::dot(w, a) - ab * glm::dot(w, b)) / denom;
  return std::isfinite(out_t);
}

// On-screen length the section drag handle holds, in pixels, measured tip to
// centre. Shared between the renderer (which scales the handle geometry by it)
// and the host (which hit-tests the same span), because a handle you can see but
// not grab — or grab but not see — is worse than no handle.
inline constexpr float kSectionHandlePx = 54.0f;

// World-space endpoints of the drag handle's shaft, tip to tip.
//
// `world_per_pixel` must be measured in the SAME pixel space the renderer draws
// in (device pixels, not the logical pixels Qt reports for mouse events), or the
// visible handle and its hit region disagree by the device pixel ratio.
inline bool section_handle_segment(const SectionPlane& plane,
                                   const scene::Aabb& bounds,
                                   float world_per_pixel,
                                   scene::vec3& out_a, scene::vec3& out_b) {
  if (!bounds.valid() || !(world_per_pixel > 0.0f)) return false;
  const scene::vec3 n = plane.unit_normal();
  const scene::vec3 origin = plane.anchor(bounds);
  const float reach = kSectionHandlePx * world_per_pixel;
  out_a = origin - n * reach;
  out_b = origin + n * reach;
  return true;
}

// Distance from `p` to the segment ab, all in 2D screen pixels. Hit-testing the
// whole shaft rather than just a tip gives the user a far more forgiving target.
inline float distance_to_segment(const scene::vec2& p, const scene::vec2& a,
                                 const scene::vec2& b) {
  const scene::vec2 ab = b - a;
  const float len2 = glm::dot(ab, ab);
  if (len2 < 1e-12f) return glm::length(p - a);
  const float t = std::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f);
  return glm::length(p - (a + ab * t));
}

// ---------------------------------------------------------------------------
// The manipulator: one translate arrow along the normal, three rotate rings.
// ---------------------------------------------------------------------------

// Which piece of the section manipulator the cursor is over, or is dragging.
// Both sides need it: the renderer to know which piece to light up, the host to
// know what a drag means.
enum class SectionGizmoPart : std::uint8_t {
  None,
  Translate,   // double-headed arrow along the plane normal — slides `offset`
  RotateX,     // rings, about the WORLD axes (see section_ring_axis)
  RotateY,
  RotateZ,
};

// Ring radius, in the same pixel space kSectionHandlePx is measured in.
// Deliberately LARGER than the arrow's reach so the rings enclose the arrow
// instead of crossing it: two manipulators that overlap on screen are two
// manipulators the user grabs the wrong one of.
inline constexpr float kSectionRingPx = 78.0f;

// A ring turns the plane about a WORLD axis rather than about one of the
// plane's own in-plane axes, for one decisive reason: SectionPlane::basis()
// derives u/v from whichever cardinal axis the normal is least aligned to, so
// they flip discontinuously as the normal sweeps past that threshold — mid-drag
// the gizmo would visibly roll over. World axes never move. They also match how
// users say it ("cut along X"), and the ring colours can then match the corner
// orientation triad exactly (renderer::kAxisColor).
inline scene::vec3 section_ring_axis(SectionGizmoPart part) {
  switch (part) {
    case SectionGizmoPart::RotateX: return {1.0f, 0.0f, 0.0f};
    case SectionGizmoPart::RotateY: return {0.0f, 1.0f, 0.0f};
    case SectionGizmoPart::RotateZ: return {0.0f, 0.0f, 1.0f};
    default:                        return {0.0f, 0.0f, 0.0f};
  }
}

// 0/1/2 for the three rings (the index into a per-axis colour table), -1 for
// anything else.
inline int section_ring_index(SectionGizmoPart part) {
  switch (part) {
    case SectionGizmoPart::RotateX: return 0;
    case SectionGizmoPart::RotateY: return 1;
    case SectionGizmoPart::RotateZ: return 2;
    default:                        return -1;
  }
}

inline bool section_part_is_ring(SectionGizmoPart part) {
  return section_ring_index(part) >= 0;
}

// How much turning this ring actually tilts the plane: |cross(n, axis)|, i.e.
// the sine of the angle between them, in [0,1]. Rotating a normal about an axis
// PARALLEL to itself leaves the plane exactly where it was, so a ring whose axis
// has drifted onto the normal is a no-op control.
inline float section_ring_gain(const SectionPlane& plane, SectionGizmoPart part) {
  const scene::vec3 axis = section_ring_axis(part);
  if (glm::dot(axis, axis) < 1e-12f) return 0.0f;
  return std::min(glm::length(glm::cross(plane.unit_normal(), axis)), 1.0f);
}

// Below this gain a ring is neither drawn nor hit-tested. A big circle lying
// flat on the cut face that does nothing when dragged is worse than no circle:
// the user concludes the gizmo is broken. It disappears instead, which reads
// correctly as "you are looking down this axis". ~8.6 degrees.
inline constexpr float kSectionRingMinGain = 0.15f;

inline bool section_ring_live(const SectionPlane& plane, SectionGizmoPart part) {
  return section_part_is_ring(part) &&
         section_ring_gain(plane, part) >= kSectionRingMinGain;
}

// The ring as `centre + e0*cos(t) + e1*sin(t)`, with |e0| = |e1| = the radius
// and cross(e0, e1) pointing along the ring's axis. One derivation for both
// sides: the renderer builds a model matrix from it, the host samples it to
// hit-test, and they cannot drift.
//
// The in-plane pair is the fixed cyclic one (X -> (Y,Z), Y -> (Z,X),
// Z -> (X,Y)) rather than anything derived from the current normal, so the ring
// parameterisation — and therefore a drag in progress — never jumps.
//
// `world_per_pixel` must be measured in the pixel space the caller works in;
// the renderer draws in device pixels, Qt reports mouse events in logical ones.
inline bool section_ring_frame(const SectionPlane& plane,
                               const scene::Aabb& bounds, SectionGizmoPart part,
                               float world_per_pixel, scene::vec3& out_centre,
                               scene::vec3& out_e0, scene::vec3& out_e1) {
  const int i = section_ring_index(part);
  if (i < 0 || !bounds.valid() || !(world_per_pixel > 0.0f)) return false;
  const float radius = kSectionRingPx * world_per_pixel;
  scene::vec3 e0(0.0f), e1(0.0f);
  e0[(i + 1) % 3] = radius;
  e1[(i + 2) % 3] = radius;
  out_centre = plane.anchor(bounds);
  out_e0     = e0;
  out_e1     = e1;
  return true;
}

inline scene::vec3 section_ring_point(const scene::vec3& centre,
                                      const scene::vec3& e0,
                                      const scene::vec3& e1, float angle) {
  return centre + e0 * std::cos(angle) + e1 * std::sin(angle);
}

// How square-on a ring must face the viewer before its plane can be used to map
// the cursor to an angle. Below this the ring is edge-on: the intersection walks
// off to infinity and a pixel of cursor movement means an unbounded rotation.
inline constexpr float kSectionRingMinFacing = 0.15f;

// Wrap to (-pi, pi]. Drag deltas are differences of two atan2 results, which
// otherwise jump by a full turn as the cursor crosses the branch cut.
inline float wrap_angle(float a) {
  constexpr float kPi    = 3.14159265358979323846f;
  constexpr float kTwoPi = 6.28318530717958647692f;
  a = std::fmod(a + kPi, kTwoPi);
  if (a < 0.0f) a += kTwoPi;
  return a - kPi;
}

// Where a screen ray crosses the ring's plane, as an angle in the ring's own
// (e0, e1) frame. This is the primary drag mapping: absolute, so the plane
// tracks the cursor exactly and a long drag accumulates no error.
//
// Returns false when the ring is too edge-on to the ray to carry angular
// information — the caller must then fall back to a screen-space mapping or
// hold the current angle, exactly the contract closest_point_on_axis() has for
// the translate arrow. Some view directions make this unavoidable: looking
// straight down world Z leaves the X and Y rings perfectly edge-on.
inline bool section_ring_angle_at(const scene::vec3& centre,
                                  const scene::vec3& e0, const scene::vec3& e1,
                                  const scene::vec3& axis,
                                  const scene::vec3& ray_origin,
                                  const scene::vec3& ray_dir, float& out_angle) {
  if (glm::dot(axis, axis) < 1e-12f || glm::dot(ray_dir, ray_dir) < 1e-12f) {
    return false;
  }
  const scene::vec3 n = glm::normalize(axis);
  const scene::vec3 d = glm::normalize(ray_dir);
  const float denom = glm::dot(n, d);
  if (std::abs(denom) < kSectionRingMinFacing) return false;

  const float t = glm::dot(n, centre - ray_origin) / denom;
  if (!std::isfinite(t)) return false;
  const scene::vec3 rel = (ray_origin + d * t) - centre;
  // e0 and e1 share a length, so scaling cancels inside atan2 and the result is
  // the true parameter angle without normalising either.
  const float x = glm::dot(rel, e0);
  const float y = glm::dot(rel, e1);
  if (std::abs(x) < 1e-20f && std::abs(y) < 1e-20f) return false;
  const float angle = std::atan2(y, x);
  if (!std::isfinite(angle)) return false;
  out_angle = angle;
  return true;
}

// Turn the plane `angle` radians about `axis`, pivoting about the point the
// gizmo currently sits on.
//
// The pivot matters. SectionPlane stores only a normal and a distance, so the
// naive rotation (spin the normal, keep the offset) sweeps the plane around the
// bounds centre — the cut visibly swings away from where the user grabbed it.
// Re-deriving the offset from the PRE-rotation anchor keeps the plane passing
// through that same point, which is what "tilt it in place" means. At offset 0
// (every preset, and the state section mode starts in) the anchor is the bounds
// centre and nothing translates at all.
//
// offset_range depends on the normal, so the result is re-clamped: a plane
// parked at the end of its travel can tilt into a shorter range.
inline SectionPlane section_plane_rotated(const SectionPlane& plane,
                                          const scene::Aabb& bounds,
                                          const scene::vec3& axis, float angle) {
  SectionPlane out = plane;
  if (!std::isfinite(angle)) return out;
  const float len2 = glm::dot(axis, axis);
  if (len2 < 1e-12f) return out;

  const scene::vec3 a  = axis * (1.0f / std::sqrt(len2));
  const scene::vec3 n0 = plane.unit_normal();
  const scene::vec3 n1 = glm::angleAxis(angle, a) * n0;
  // NaN fails every comparison, so this rejects a degenerate rotation outright
  // rather than letting a NaN plane reach the clip equation.
  if (!(glm::dot(n1, n1) > 1e-12f)) return out;

  out.normal = glm::normalize(n1);
  const scene::vec3 centre =
    bounds.valid() ? bounds.center() : scene::vec3(0.0f);
  out.offset = glm::dot(out.normal, plane.anchor(bounds) - centre);
  const float range = out.offset_range(bounds);
  out.offset = std::clamp(out.offset, -range, range);
  return out;
}

// The plane a "Reset" puts back: centred, and pointing along whichever world
// axis it was already nearest (sign kept, so a reset never flips which half of
// the model survives).
inline SectionPlane section_plane_reset(const SectionPlane& plane) {
  const scene::vec3 n = plane.unit_normal();
  int best = 0;
  for (int i = 1; i < 3; ++i) {
    if (std::abs(n[i]) > std::abs(n[best])) best = i;
  }
  SectionPlane out;
  out.normal       = scene::vec3(0.0f);
  out.normal[best] = n[best] >= 0.0f ? 1.0f : -1.0f;
  out.offset       = 0.0f;
  return out;
}

// True when a reset would change nothing — the plane is already axis-aligned and
// centred. Lets the UI grey the command out instead of offering a click that
// does nothing. The offset tolerance is scaled to the model's travel so it means
// the same on a 5 mm part and a 5 m one.
inline bool section_plane_is_reset(const SectionPlane& plane,
                                   const scene::Aabb& bounds) {
  if (std::abs(plane.offset) > 1e-4f * plane.offset_range(bounds)) return false;
  const scene::vec3 n = plane.unit_normal();
  for (int i = 0; i < 3; ++i) {
    if (std::abs(std::abs(n[i]) - 1.0f) < 1e-3f) return true;
  }
  return false;
}

} // namespace cadly::renderer

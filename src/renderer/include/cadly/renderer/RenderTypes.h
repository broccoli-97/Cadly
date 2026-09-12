#pragma once

#include "cadly/renderer/SectionPlane.h"
#include "cadly/scene/Math.h"

#include <cstdint>

namespace cadly::renderer {

// Opaque GPU handle. The renderer chooses how to populate the value; the rest
// of the code only checks against `invalid`.
struct ResourceHandle {
  std::uint64_t value{0};
  bool valid() const { return value != 0; }
  bool operator==(const ResourceHandle& o) const { return value == o.value; }
  bool operator!=(const ResourceHandle& o) const { return value != o.value; }
};

struct MeshHandle     : ResourceHandle {};
struct MaterialHandle : ResourceHandle {};

// The app's world-axis palette (X, Y, Z), in sRGB — written straight out by the
// overlay and section shaders, no linearisation. Shared by the corner
// orientation triad and the section rotate rings so that a red ring and the
// triad's red X arm are unmistakably the same axis; two different reds would
// quietly undo the one thing the colour coding is for. Desaturated on purpose:
// full-strength RGB fights the Graphite shell and competes with the
// signal-orange selection wash.
inline const scene::vec3 kAxisColor[3] = {
  {0.84f, 0.30f, 0.33f},
  {0.30f, 0.62f, 0.38f},
  {0.32f, 0.49f, 0.83f},
};

// Display options pushed in by the UI layer. The renderer interprets them; the
// scene itself doesn't need to know what's on.
struct DisplayMode {
  bool wireframe        {false};
  // Technical-drawing view: render flat, unlit faces into colour + depth,
  // then draw the BRep edges and view-dependent silhouette contours that
  // pass the depth test; occluded lines re-appear dimmed when
  // `show_hidden_edges` is on.
  bool hidden_line      {false};
  // Hidden-line only: also draw the lines the depth test rejected — edges
  // and silhouettes occluded by the model — in a dimmed ink (mixed toward
  // the paper colour). This is the Creo/SolidWorks "Hidden Line" style,
  // where hidden structure stays readable in grey; switching it off gives
  // the stricter "No Hidden" drafting style (hidden lines removed).
  bool show_hidden_edges {true};
  bool show_edges       {true};    // outline at sharp creases
  bool show_axes        {true};    // world-axes triad overlay (corner)
  bool show_scale_bar   {true};    // fixed-length scale bar overlay (corner)
  bool draw_background  {true};
  // Debug overlay: draw every triangle edge of the face triangulation on
  // top of the shaded surface. Independent of show_edges (BRep edges) and
  // wireframe (BRep-only line view); orthogonal to both.
  bool show_triangle_mesh {false};
  float edge_intensity  {0.65f};
  scene::vec3 hidden_line_color{0.82f, 0.84f, 0.88f};
  scene::vec3 background_top   {0.42f, 0.44f, 0.48f};
  scene::vec3 background_bottom{0.22f, 0.23f, 0.26f};
  // Shaded back faces of open surfaces and sectioned solids use a matte cool
  // tint. Supplied in sRGB; the surface shader linearises it before lighting.
  scene::vec3 backface_color   {0.471f, 0.600f, 0.678f};

  // Rotation pivot indicator. The UI toggles this on while the user is
  // orbiting and feeds the world-space pivot through `rotation_pivot`. The
  // renderer draws a small screen-space-stable marker on top of the scene.
  bool        show_rotation_pivot{false};
  scene::vec3 rotation_pivot     {0.0f};

  // Selection highlight + isolate ghosting (driven by the sidebar tree).
  // `selection_color` arrives in sRGB — the PBR shader applies the tint
  // after tonemapping/gamma, so no linearisation on this side. Nodes with
  // Node::selected get an unlit wash of it blended at `selection_opacity`:
  // the wash is constant by design — independent of lights and view — so
  // the highlight looks identical from every camera angle (the previous
  // rim-weighted lit tint read as the selection colour itself changing
  // while orbiting), and the sub-1 opacity keeps the part's own shading
  // visible through the film so it still reads as 3D geometry. Selected
  // edges draw in the colour outright with a slightly heavier stroke in
  // every display mode; selected edges hidden behind geometry stay visible
  // only as a thinner, much fainter ghost — enough to locate a buried part,
  // but clearly subordinate to the visible outline so occlusion still
  // reads and the part keeps its depth. The face wash stays normally
  // depth-tested. Nodes with Node::ghosted (isolate mode) render as a
  // translucent veil at `ghost_opacity` — full-detail geometry, but faded
  // so the focused part carries the frame. The default colour matches the
  // dark theme's viewport_highlight token: a saturated signal orange,
  // chosen over the accent blue because nothing else in a typical CAD
  // scene is orange (the UI overwrites per theme).
  scene::vec3 selection_color  {1.0f, 0.478f, 0.149f};
  float       selection_opacity{0.65f};
  float       ghost_opacity    {0.12f};
  // Isolate's second presentation style: when set, nodes with Node::ghosted
  // are omitted from the frame entirely — no veil, no wireframe's faded
  // lines, not even selection strokes (a hidden part with floating
  // selection ink would read as present). The flag lives here rather than
  // in the scene so the UI can flip veil <-> hidden per frame without
  // re-walking the node hierarchy; Node::ghosted keeps marking "outside
  // the isolate focus" either way. Ignored while nothing is ghosted.
  bool        hide_ghosted     {false};

  // Section view (剖切): one clip plane that cuts the model open so internal
  // features can be inspected, with the exposed cut face filled in so the
  // solid still reads as solid rather than as a hollow shell.
  //
  // The clip itself is a half-space test in the vertex/geometry stages
  // (gl_ClipDistance) applied to surfaces, BRep edges, and silhouettes alike —
  // anything less and the model would be cut in one pass and whole in another.
  // The fill ("cap") is a separate stencil pass: the renderer counts front vs
  // back faces of the clipped geometry per pixel, and wherever the two do not
  // balance, the plane has passed through material and the cap quad draws
  // there. See SectionPass for the algorithm and why counting rather than
  // parity is required.
  //
  // Wireframe mode is clipped but NOT capped: there is no filled surface to
  // cap, and a solid patch floating among BRep lines would read as an error.
  bool         section_enabled   {false};
  // The plane. `offset` is measured from the scene bounds centre, so the
  // default cuts through the middle of whatever was imported and the UI gets a
  // symmetric drag range — see SectionPlane.
  SectionPlane section           {};
  // Draw the translucent plane quad and the manipulator on it: the arrow that
  // slides the plane along its normal, and the three world-axis rings that tilt
  // it. Switching this off keeps the cut but hides the manipulator, which is
  // what a user wants once the plane is where they want it (and what
  // screenshots want). Like `show_hidden_edges`, the flag IS the user's
  // preference — the renderer just ignores it while the section is off, so the
  // UI never force-clears it.
  bool         section_show_plane{true};
  // Hatch the cut face with diagonal drafting strokes, evaluated in plane
  // space so they do not swim while orbiting. In hidden-line mode the hatch is
  // what makes the cap read as a section rather than as a blank patch of paper.
  bool         section_hatch     {true};
  // Blend the cut face over interior geometry in shaded mode. Hidden-line
  // sections keep opaque paper and depth so their occluded edges stay dimmed.
  bool         section_translucent{true};
  // Hover/drag feedback on the manipulator, driven by the host's screen-space
  // hit-test: which piece the cursor is over, or is currently dragging.
  // Transient view state like `show_rotation_pivot`, not a preference. The
  // renderer highlights that piece; None means nothing is hot.
  SectionGizmoPart section_hot_part{SectionGizmoPart::None};
  // Cut-face fill, in framebuffer sRGB like `selection_color`. The
  // pale copper default matches the dark theme's viewport_section token;
  // the UI overwrites it per theme.
  scene::vec3  section_cap_color {0.906f, 0.749f, 0.588f};

  // Anti-aliasing. Off (0 or 1) renders directly to the host's default
  // framebuffer; any other value asks the renderer to allocate an offscreen
  // multisample colour+depth target with that many samples, draw the whole
  // frame into it, and resolve to the default framebuffer at the end. The
  // renderer clamps the request to GL_MAX_SAMPLES on the running GPU, so
  // asking for 16 on a card that only supports 8 falls back gracefully.
  // Typical values: 0 (off), 2, 4, 8.
  int msaa_samples{4};
};

} // namespace cadly::renderer

# CLAUDE.md

Guidance for working in this repository.

## What Cadly is

A native C++17 desktop CAD **viewer** (not editor). It imports STEP/IGES via
Open CASCADE (OCCT), triangulates the B-Rep, and renders it with a hand-written
real-time renderer (OpenGL 4.1 core today; the interface is built to take a
Vulkan backend later). Qt 6 Widgets provides the shell. PBR metallic-roughness
materials with image-based lighting, tuned for industrial inspection.

`docs/cad-viewer-plan.md` is the original design plan and milestone map — read it
for intent and scope (non-goals, risks, the renderer/import strategy). Treat it
as the north star, not a status report; the code has moved past parts of it.

## Build & run

Uses CMake presets (Ninja). System Qt6 + OCCT on Linux; vcpkg manifest
(`vcpkg.json`) for Windows/portable builds; Homebrew on macOS
(`scripts/setup-macos.sh` installs/verifies the formulae).

```bash
cmake --preset linux-release          # configure (RelWithDebInfo)
cmake --build --preset linux-release  # build -> build/linux-release/bin/
ctest   --preset linux-release        # run smoke tests

build/linux-release/bin/cadly [file.step]   # GUI; optional file opens at startup
build/linux-release/bin/cad_import_cli f.step  # headless import, prints geo stats
```

Presets: `linux-debug`, `linux-release`, `linux-qt68-{debug,release}` (Qt 6.8
from `~/Qt/6.8.3/gcc_64`, enables the qlementine style), `linux-vcpkg-debug`,
`macos-{debug,release}` (Homebrew Qt ≥ 6.8 + OCCT; the GUI builds as
`bin/cadly.app`, so the binary is `bin/cadly.app/Contents/MacOS/cadly` —
per-platform behaviour differences live in `docs/platform-divergence.md`),
`windows-msvc-{debug,release}` (VS solution), `windows-ninja-{debug,release}`
(single-config Ninja; expects MSVC in the environment, i.e. a VS dev prompt —
what CI uses, since it can't pin a VS-year generator to a rotating runner
image). `windows-ninja-release` pins the release-only `x64-windows-release`
triplet — a release build never links the debug deps, so vcpkg skips building
debug Qt/OCCT (half the cold dependency build). The debug and VS presets keep
stock `x64-windows`; the VS one because its multi-config generator can still
build a Debug config. The build/test/package pipeline lives once, in the
reusable `.github/workflows/build-package.yml`; `ci.yml` calls it on every
push/PR and `release.yml` calls it on `v*` tags (or a manual run) and attaches
the packages to a draft GitHub Release. Keep packaging changes in
`build-package.yml` so both paths ship the same thing; the smoke test and a
headless STEP import are the gates.

`cad_import_cli` is the fastest way to validate an import change without a GL
context or display — prefer it when touching `src/cad`.

## Module layout & the dependency rule

One CMake target per module under `src/`, exported as `Cadly::<Name>` aliases.
Public headers live in `src/<module>/include/cadly/<module>/`; private
implementation headers sit next to the `.cpp` in `src/<module>/src/`. Namespace
is `cadly::<module>`.

| Module           | Target              | Depends on                       | Links |
|------------------|---------------------|----------------------------------|-------|
| `platform`       | `Cadly::Platform`   | spdlog, fmt                      | logging, asset/path lookup |
| `scene`          | `Cadly::Scene`      | glm **only**                     | renderer-/Qt-/OCCT-free canonical model |
| `renderer`       | `Cadly::Renderer`   | scene (INTERFACE/header-only)    | `IRenderer`, `RenderTypes` |
| `renderer_gl`    | `Cadly::RendererGL` | renderer, scene                  | OpenGL 4.1 backend, **Qt-free** |
| `cad`            | `Cadly::Cad`        | scene                            | **only** target that links OCCT |
| `ui`             | `Cadly::Ui`         | scene, renderer, renderer_gl, cad, Qt6 | widgets, viewport host |
| `app`            | `cadly` (exe)       | ui, platform, Qt6                | entry point, settings, recent files |

**Hard invariants — preserve these, they're the architecture:**

- `scene` must **not** depend on Qt, OCCT, OpenGL, or Vulkan. Only glm (+ the
  logging shim). It is the stable contract between importers and renderers.
- Only `cad` links OCCT toolkits. The `TK*` list is curated in
  `src/cad/CMakeLists.txt` (handles both OCCT 7.6 and 7.7+ target names) — don't
  link the omnibus `OpenCASCADE::*` alias.
- `renderer_gl` is **Qt-free**: the host passes in a GL function-pointer loader
  (`GLLoadProc`) so the backend works under Qt/SDL/GLFW/EGL/glX. Qt wiring lives
  only in `src/ui/ViewportWidget.cpp`. Don't `#include <Q...>` in `renderer_gl`.

## Data flow

File → `ImporterRegistry::select()` picks an importer by extension →
`OcctStepImporter`/`OcctIgesImporter` (CAF readers for colors/names/hierarchy,
plain readers as geometry-only fallback) → `BRepMesh_IncrementalMesh` →
`OcctShapeToMesh` converts faces to `scene::Mesh` → `scene::Scene` →
`IRenderer::attach_scene()` → `render()`.

Import runs on a worker thread (`QtConcurrent`/`std::thread`); it is **non-modal**
— progress and cancel live in the document tab and status bar,
and the previous scene stays interactive throughout (`MainWindow::importing_`
disables only the Open entry points, not the whole UI). Importers MUST poll
`IProgressSink::cancelled()` between heavy steps. `ImportOptions` (in
`ICadImporter.h`) holds the meshing knobs (linear/angular deflection, healing,
welding) — defaults are tuned for mm-scale industrial assemblies, visualization
quality not simulation. The Inspector's **Import** tab is the persistent editor
for these; importing is immediate with the persisted options unless "Review
options before each import" (or `⌥⌘O` / File ▸ Open with Options…) is set.

## Renderer lifetime contract

`IRenderer`: `initialize()` (context current) → per frame `attach_scene()` *only
if changed* then `render(DisplayMode)` → `shutdown()` (context still current).
`ViewportWidget` tracks `scene_dirty_` so it attaches at most once per scene
swap, never per frame. GPU mesh buffers upload lazily inside `render()`, keyed by
a `shared_ptr<Mesh>` (the strong ref guards against `Mesh*` address reuse).

`DisplayMode` (`RenderTypes.h`) is how the UI talks to the renderer — the scene
itself doesn't know what overlays are on.

### Surface and edge modes

Easy to conflate; they are deliberately separate (see `scene::Mesh` doc comments):

1. **Show edges** (`show_edges`) — the default "shaded with edges" overlay. Uses
   `Mesh::edge_strip_indices`, BRep edges sampled *exactly* on the face
   triangulation nodes, so polygon offset alone keeps them in front (no Z-fight).
2. **Hidden line** (`hidden_line`) — flat, unlit faces write colour and depth,
   then mesh-coupled BRep edges and silhouette contours draw over them: visible
   lines in ink, occluded lines re-drawn dimmed via a second pass with
   `GL_GREATER` (Creo's "Hidden Line" style; `DisplayMode::show_hidden_edges`
   off gives the stricter "No Hidden" — user-facing as the "Dimmed Hidden
   Lines" toggle in the Hidden Line segment's chevron menu, persisted in
   `display/dimmed_hidden`). Do not replace the filled depth prepass with
   plain wireframe rendering.
3. **Wireframe** (`wireframe`) — BRep edges *without* surfaces. Uses
   `Mesh::edge_lods`, an analytical LOD ladder selected per frame by
   world-per-pixel scale (refines on zoom), plus silhouette contours. Mutually
   exclusive with the triangle mesh overlay.
4. **Triangle mesh** (`show_triangle_mesh`) — debug overlay drawing every
   triangle edge of the tessellation.

Edge polylines are `GL_LINE_STRIP` runs terminated by the `0xFFFFFFFF`
primitive-restart sentinel, one drawcall per tier.

**Silhouettes** (the view-dependent contours where a curved surface rolls away
— cylinder sides, sphere outlines) are not BRep edges and can't come from the
edge buffers. Both line modes get them from a per-frame GPU pass
(`silhouette.vert/.geom` + the edges fragment stage): a geometry shader reads
the surface triangles through the PBR VAO and emits the zero-crossing of
`dot(N, toward-eye)` interpolated *inside* each triangle — smooth sub-facet
contours, no adjacency data, no CPU recompute while orbiting. Each crossing is
then lifted from the triangle's chord onto the smooth surface using the
directional normal curvature along that edge (`0.5 * dot(delta-normal,
delta-position)`, signed for concave bores — see `surface_crossing` in
`silhouette.geom`). Do not replace the dot product with the product of vector
lengths: a cylinder mesh's diagonal edge also spans its zero-curvature axial
direction, and treating that full length as curved makes the generator bulge
outward while orbiting. In hidden-line mode only, the reconstructed contour
gets a fixed 0.65 px outward shift after projection; this provides stable
raster coverage against the filled depth surface without deforming the
world-space curve or changing its depth against other geometry. Wireframe has
no filled surface and therefore gets no screen-space shift. Shaded mode
deliberately draws no silhouette lines (shading carries the contour;
commercial CAD does the same). Relatedly, the importer skips
parametric **seam edges** (`BRep_Tool::IsClosed(edge, face)` — the closing
edge of cylinder/cone/sphere/torus faces) in both edge buffers: no commercial
viewer inks seams, and the silhouette pass now supplies curved faces' visual
presence instead.

### Section view (剖切)

`SectionPass` (`src/renderer_gl/src/SectionPass.{h,cpp}`) is a self-contained
component: it owns its program, buffers, and GL state, and `render()` only gains
guarded calls. Two separable halves:

1. **The clip** — `gl_ClipDistance[0]` against `u_clip_plane` (a `vec4` in the
   shared `FrameBlock` UBO), written by `pbr.vert`, `edges.vert`, and
   `silhouette.geom`. The geometry stage is where the silhouette program must
   write it (user clipping runs after the *last* vertex-processing stage, and
   `silhouette.vert` deliberately doesn't emit `gl_Position`), and it must be
   re-set before **each** `EmitVertex()` — outputs go undefined after one.
   There is no "enabled" uniform: the C++ side just calls
   `glEnable/glDisable(GL_CLIP_DISTANCE0)`.
   **The invariant that creates:** while that is enabled, every program that
   draws must write `gl_ClipDistance[0]` — an unwritten distance is *undefined*
   per spec, not zero. `draw_overlays()` exists as the single funnel where the
   clip is switched off before the pivot/triad/scale-bar programs run, and
   `render()` carries a `ClipOnExit` RAII guard for the same reason
   `ResolveOnExit` exists: several early returns.
2. **The cap** — the stencil fill that keeps a cut solid from reading as a
   hollow shell. Per frame: count front vs back faces of the *clipped* geometry
   with `INCR_WRAP`/`DECR_WRAP` (colour + depth writes off, depth test off, cull
   off), then fill wherever the count is non-zero.

Things not to "simplify" in the cap:

- **Counting, not parity.** `GL_INVERT` is shorter and is what most tutorials
  show, but two bodies cut by the same plane invert twice, the parity returns to
  even, and the cap vanishes on exactly the assemblies this feature is for.
- **The counting pass must run with clipping ON**, and its program must write a
  clip distance. Uncut, every closed solid balances everywhere and no cap is
  ever produced. It borrows `prog_edges_` — the only position-only program that
  already binds `FrameBlock`, takes `u_model`, and reads attribute 0 from the
  surface VAO — rather than carrying a program of its own.
- **`GL_NOTEQUAL 0`, not a sign test.** That is what makes mirrored instances
  free: a negative-determinant world matrix swaps front and back, so a cut body
  counts +1 instead of −1.
- **The cap polygon is plane ∩ scene-AABB** (`plane_box_cross_section`), not a
  square. A square of half-size *r* has corners at *r*√2, which fall outside the
  near/far slab `CameraController` fits to the bounding *sphere* — the cap comes
  back with notches bitten out of it. Every vertex of this polygon is inside the
  AABB by construction, and it doubles as the gizmo's translucent plane.
- **Only straddling nodes are counted** (`box_straddles_plane` against
  `Node::world_bounds`). A node wholly on the kept side contributes a matched
  front/back pair — net zero — and one wholly cut away contributes nothing, so
  the pass touches a handful of parts instead of the whole assembly.
- **The cap plane is biased +ε toward the kept side.** A model face lying
  exactly on the section plane is kept (`>= 0`) and would otherwise render at
  precisely the cap's depth; users drag the plane onto planar features
  constantly.
- **Restore the GL baseline on exit.** A leaked `glColorMask(0,…)` defeats the
  *next* frame's `glClear(GL_COLOR_BUFFER_BIT)`, and a leaked `GL_STENCIL_TEST`
  masks every later pass to the cap's footprint. The cap draw uses
  `glStencilOp(KEEP,KEEP,KEEP)` rather than `glStencilMask(0)` precisely because
  a zero write mask also gates `glClear(GL_STENCIL_BUFFER_BIT)`.

Mode behaviour: **Wireframe is clipped but not capped** — there is no filled
surface to look hollow, and a solid patch among BRep lines reads as an error. In
**Hidden Line** the cap fills with the paper colour, gamma-encoded to match
`pbr.frag:100` exactly, and the hatch is drawn *unconditionally* — the shell
sets `hidden_line_color` to the background, so paper-on-paper would be
indistinguishable from a hole; the `section_hatch` toggle governs shaded mode,
where the solid fill already reads. Ghosted parts are clipped but not capped
(the veil writes no depth anyway).

Known limitation: the count assumes closed, consistently wound geometry.
`Mesh::double_sided` meshes are skipped, but that flag is set per imported
*shape* (`is_cull_safe` in `OcctShapeToMesh.cpp`), so a compound of one solid
plus loose sheet bodies isn't flagged and its unmatched faces can raise the
count where no material was cut — a phantom cap plate. Fixing it needs a
per-solid closedness flag from the importer.

Interaction is screen-space, not GPU picking (`IRenderer::pick()` is still a
stub): `ViewportWidget` projects the manipulator with
`CameraController::project_to_screen()` and measures point-to-*segment*
distance, then maps drags through `screen_ray()`. The manipulator's world size
comes from `renderer::kSectionHandlePx` / `kSectionRingPx` — shared by both
sides, since a handle you can see but not grab is worse than none. Mind the
pixel spaces: `CameraController` works in **logical** pixels (what
`QMouseEvent::pos()` reports), the renderer in **device** pixels, so the
hit-test divides by the device pixel ratio.

The manipulator has two controls, and `DisplayMode::section_hot_part`
(a `SectionGizmoPart`) is how the host tells the renderer which one to light up:

- **Translate** — the double-headed arrow along the normal, dragged through
  `closest_point_on_axis()`. It gets first refusal on a press: it is drawn
  *inside* the rings, and sliding is the more common action, so an ambiguous
  press near the centre should move rather than tilt.
- **Rotate** — three rings about the **world** X/Y/Z axes, coloured from
  `renderer::kAxisColor` (the same table the corner triad reads, so a red ring
  and the red X arm mean the same axis). Shift snaps a drag to 15°; the banner
  shows the live angle.

Things not to "simplify" in the rings:

- **World axes, not the plane's own u/v.** `SectionPlane::basis()` seeds off
  whichever cardinal axis the normal is least aligned to, so u/v flip
  discontinuously as the normal sweeps past that threshold — mid-drag the gizmo
  would visibly roll over. World axes never move, and they match how users say
  it ("cut along X").
- **A ring whose axis has drifted onto the normal is not drawn and not
  hit-tested** (`section_ring_live()`, gain = `|cross(n, axis)|`). Rotating a
  normal about an axis parallel to itself changes nothing, so drawing it would
  put a large circle on the cut face that does nothing when dragged — which
  reads as a broken gizmo, not as a redundant control.
- **Two drag mappings, deliberately.** The primary one intersects the cursor ray
  with the ring's own plane and reads the angle there: absolute, so a long drag
  accumulates no error. It has no answer for an edge-on ring — and that is the
  *default* state, because section mode starts with the normal down the view
  axis, which leaves the two useful rings exactly edge-on. So
  `section_ring_angle_at()` refuses (same "caller holds" contract as
  `closest_point_on_axis`) and `ViewportWidget` falls back to mapping cursor
  motion onto the ring's projected tangent. Both write one accumulator, so a
  drag crossing between the regimes doesn't jump.
- **A drag measures against the plane as it was at mouse-down**, not the live
  one. Tilting an off-centre plane slides its anchor (the perpendicular foot
  moves even though the plane still passes through the old one), so re-deriving
  the ring each frame would let the ring centre chase the cursor that is turning
  it. `ViewportWidget` holds `section_drag_start_plane_` for the whole drag and
  re-derives the result from it absolutely.
- **Rotation pivots about the anchor, not the bounds centre.**
  `section_plane_rotated()` re-derives `offset` from the pre-rotation anchor so
  the plane keeps passing through the point the gizmo sits on — "tilt in place".
  Spinning the normal and keeping the offset would swing the cut away from where
  the user grabbed it. `offset_range` depends on the normal, so the result is
  re-clamped.
- **The axis presets are an `ExclusiveOptional` group.** A tilt can leave the
  plane on no world axis, and a plain exclusive group forces the last tick back
  on — the menu would then claim an orientation the plane is not in.
  `sync_section_axis_actions()` re-derives the ticks from the normal;
  `tests/toolbar_test.cpp` pins the policy.

`renderer::SectionPlane` (`src/renderer/include/cadly/renderer/SectionPlane.h`)
holds the shared math — it lives in the header-only `renderer` module because
`renderer_gl` and `ui` must derive the plane identically or the drawn gizmo and
its hit region drift apart. `offset` is measured from the **scene bounds
centre**, so 0 always cuts through the middle of whatever was imported;
`-offset_range` clears the near side (nothing cut) and `+offset_range` passes
the far side (everything cut). **Reset Plane** (`section_plane_reset()`) undoes
both halves — recentres *and* squares the normal back to the nearest world axis,
sign kept — because the rings can leave the plane anywhere; it greys itself out
via `section_plane_is_reset()` rather than offering a click that does nothing.

### Rendering gotchas (don't "fix" these)

- **MSAA is renderer-owned.** The Qt surface is single-sample on purpose
  (`main.cpp` / `ViewportWidget` set samples 0); the renderer draws into its own
  offscreen multisample FBO (`DisplayMode::msaa_samples`, clamped to
  `GL_MAX_SAMPLES`) and resolves at end of frame. Asking Qt for a multisample
  default framebuffer would break the resolve.
- **Surface alpha is 0** (`setAlphaBufferSize(0)`) so the OS compositor treats
  the window as opaque and blended passes can't bleed the desktop through.
  Renderer also clears alpha=1 with alpha-preserving blend funcs.
- `scene::Vertex` is `#pragma pack(1)`, 28 bytes, asserted via `static_assert`.
  The GL vertex-attribute layout assumes exactly this — keep them in sync.

## UI conventions

The shell is the "Graphite" layout (a 52px custom-painted `ToolbarWidget` and
document `QTabBar` over `QSplitter{SidebarWidget | (ViewportWidget /
DiagnosticsStrip) | InspectorWidget}` and a 26px status bar) — see
`docs/ui-redesign/`. Each tab owns a file path, scene, camera, visibility state,
and import summary; the OpenGL viewport is shared and re-attached on tab
switches. Panels are fixed-position and toggle visibility only; the old
`QDockWidget` shell is gone. Layout persists as explicit `QSettings` keys.

- Mouse: bindings come from the selected **navigation scheme**
  (`NavigationScheme.h` — Cadly default, Blender, Rhino, Fusion 360, Maya;
  edited in **Preferences ▸ Navigation** with a legend generated from the
  live binding table; persisted as `display/navigation_scheme`, orbit style
  as `display/orbit_style`). Cadly default: right-drag orbits,
  middle-drag pans. Every scheme also answers **Alt+left-drag orbit,
  Alt+Ctrl+left-drag pan** (⌥ / ⌥⌘ on macOS — the trackpad path; trackpads
  have no middle button), and a modifier arriving a beat after the press
  still promotes the drag (synthesized three-finger drags deliver exactly
  that). *Plain* left never resolves in any scheme — reserved for picking
  (not yet implemented, passed through) — except that the section manipulator
  claims it first while section mode is on. A manipulator drag in flight
  outranks mid-gesture modifier promotion, so Alt cannot turn a ring drag into
  an orbit. Wheel zoom anchors on the point under the cursor. Orbit uses a
  quaternion camera around a pluggable
  `RotationPivotResolver` (default: camera target).
- Sidebar tree: click selects and **highlights** the part in the viewport
  (`Node::selected` → an unlit, semi-transparent signal-orange wash —
  `ThemeTokens::viewport_highlight` blended at `DisplayMode::selection_opacity`.
  Unlit so the highlight reads identically from every angle; not the accent,
  which vanished against the grey parts). Left-clicking the viewport or the
  tree's blank area cancels the highlight (left is otherwise reserved for
  picking, not yet implemented); double-click **isolates** it — everything
  else ghosts translucent (`Node::ghosted`) and grays in the tree, with a
  floating Back banner over the viewport as the exit (Esc works too; isolate
  unwinds before zero-chrome). The banner also holds the mode's one option, a
  "Hide Others" toggle (View menu twin next to Exit Isolate): checked, the
  parts outside the focus disappear outright instead of ghosting
  (`DisplayMode::hide_ghosted` — `Node::ghosted` still marks them; only the
  renderer's treatment changes). Like Dimmed Hidden Lines, the checkbox is
  the preference itself (`display/isolate_hide_others`), never
  force-cleared, enabled/visible only while isolate is active. Isolate
  persists per tab via `DocumentState::isolate_node`; the flags live in the
  scene's nodes but the sidebar wipes them on every scene handover and the
  shell re-applies.
- Section mode (剖切) is a checkable toolbar split-button (`[Section][▾]`,
  the same joined main+chevron construction as Open) sitting after the surface
  segments behind a separator — it is orthogonal to Shaded/Hidden Line/
  Wireframe, not a fourth way to shade. Its chevron menu and the View ▸ Section
  submenu share one `QMenu` and the same `QAction` instances, so they cannot
  drift. A `SectionBanner` capsule pins to the viewport's **bottom**-centre
  (isolate's is top-centre, the HUD is top-right, so all three coexist) with a
  live offset readout, Flip, and Exit; while a rotate ring is being dragged it
  also shows the drag's angle, which is transient and cleared on release (the
  plane's orientation is described by its axis label, not by how far the last
  drag turned it). The plane and its enabled flag persist
  per tab via `DocumentState::section`; only the two style toggles persist to
  QSettings (`display/section_show_plane`, `display/section_hatch`) — an offset
  is model-scaled and meaningless against the next file opened. `finish_import`
  resets the offset but keeps the orientation. `apply_section_mode_ui()` is
  split out of `on_section_toggled()` so restoring a tab never re-runs the
  first-entry "point the plane down the view axis" setup and clobbers the plane
  it is restoring. Esc unwinds section → isolate → zero-chrome.
- Shortcuts: `F` fit, `W` wireframe, `H` hidden line, `E` edges, `T` triangle mesh,
  `S` section, `P`
  perspective toggle (ortho is default for CAD). Standard views `1`-`7`
  (Front/Back/Right/Left/Top/Bottom/Iso, Blender-style numbering). `⌃.` toggles
  zero-chrome (all panels hidden; Esc restores); `⌥⌘O` opens with the import
  pre-flight; `Ctrl+W` closes the active document tab.
- Surface modes are visibly exclusive in the toolbar
  `Shaded|Hidden Line|Wireframe` `SegmentedControl`. Edges/Mesh are **not**
  standalone toolbar chips — they live in the Shaded segment's chevron menu
  (`tests/toolbar_test.cpp` asserts this) and are **disabled-but-remembered** in
  the other two modes. Hidden
  Line's chevron menu holds "Dimmed Hidden Lines" (`show_hidden_edges`);
  its checked state is the preference itself, so it is only ever
  enabled/disabled with the mode, never force-cleared.
- **Preferences** (`PreferencesDialog`, non-modal, instant-apply): General
  (language, dark appearance — routed through the existing menu actions so
  the twins stay in sync) and Navigation (scheme + orbit style). The action
  carries `PreferencesRole`, so macOS relocates it to the app menu as
  Settings… ⌘,; elsewhere it lives at File ▸ Preferences…. Dividing line:
  Inspector = live view/document controls, Preferences = set-once app
  behavior.
- Custom-painted widgets (`ToolbarButton`, `SegmentedControl`,
  the sidebar delegate, `Popover`, …) read `ui::ThemeTokens` (a struct, **not**
  `QPalette`) so they render identically under Fusion (Qt 6.4) and qlementine
  (Qt 6.8). `ThemeManager::changed` drives a live dark/light swap;
  `app::apply_theme(app, dark)` keeps `QStyle`/`QPalette` in step and is safe to
  re-call at runtime. Build the panels *before* the menus (the View menu wires
  their toggle actions).
- On Qt >= 6.8, Dark and Light both use one persistent qlementine QStyle;
  appearance changes switch its loaded JSON theme without replacing QStyle.
- On Qt < 6.8, `app::apply_theme` layers a narrowly scoped Graphite QSS over
  Fusion for stock menus, fields, spin boxes, group boxes, sliders, and
  scrollbars. Do not install a second app-wide stylesheet from the UI module.
  Reuse the existing Fusion style instance on theme changes: replacing it can
  recreate `QOpenGLWidget`'s backing surface and invalidate renderer resources.
- Dev aid: `cadly --screenshot <png> [--demo hiddenline|wireframe|light|views|getinfo|shadedmenu|hiddenmenu|zerochrome|preferences|highlight|isolate|isolate-hide|section]`
  drives a UI state and grabs it headlessly (used to verify the shell without an
  input-injection tool). `--demo hiddenline-orbit:<yaw>,<pitch>[,persp]` (and
  the `wireframe-orbit:` twin) screenshots a line mode at an exact arbitrary
  orientation — the silhouette pass is view-dependent, so regressions hide at
  in-between azimuths the seven standard views never hit. The `section*` states
  cover the cap algorithm's cases: `section-hiddenline` / `section-wireframe`
  for the per-mode treatments, `section-noplane` for the cut without the
  manipulator, `section:<fraction>` to place the plane at a fraction of the
  model's travel (scale-independent, so one string works on any file), and
  `section-behind` to orbit round to the kept side, where the cap must vanish
  and the intact outer surface must show — the half of the depth argument no
  front-side screenshot can demonstrate. `section-rotate` tilts the plane 35°
  about world X, which is the only headless way to check the rotate rings: all
  three appear once the normal is off every axis, and the cap must still fill a
  skew cross-section. `--lang zh_CN|en`
  overrides the UI language for one run without touching the persisted setting
  (how translated screenshots are taken).

## Localization

UI language is Qt i18n: catalogs live in `translations/cadly_*.ts` (zh_CN
today), compiled by lrelease and embedded under the `:/i18n` resource prefix,
loaded in `main.cpp` before any widget exists — the shell sets its strings
once at construction, so a language change applies on the next launch (the
View ▸ Language menu offers the relaunch). The choice persists as
`ui/language` (`system|en|zh_CN`); language display names in the menu are
deliberately not translated (each language names itself). Qt6 LinguistTools
is an **optional** dependency: without it the build succeeds English-only
(vcpkg gets lrelease via qttools' `linguist` feature). After adding or
changing `tr()` strings, refresh catalogs with the `update_translations`
build target (or `/usr/lib/qt6/bin/lupdate -locations none src/ui/src
src/ui/include src/app/src src/app/include -ts translations/cadly_zh_CN.ts`)
and fill in the new entries. Strings in tables must be marked `QT_TR_NOOP`
(see InspectorWidget's tab specs); classes without `Q_OBJECT` must use
`QCoreApplication::translate` with an explicit context, never inherited
`tr()` (see IsolateBanner).

## Assets & logging

- Runtime asset lookup: `platform::find_asset_dir(subdir)` tries
  `$CADLY_ASSET_ROOT`, then several paths near the executable, then
  `CADLY_SOURCE_ROOT` (a compile def) so **dev builds find shaders in the source
  tree without installing**. Shaders load from `shaders/glsl` via
  `load_shader_source()`.
- GLSL shaders are plain files in `shaders/glsl/` (installed to
  `share/cadly/shaders`), not compiled in. PBR, IBL (env capture / irradiance /
  prefilter / BRDF LUT), background gradient, edges, pivot marker.
- Logging: `CADLY_LOG_*` macros (spdlog). `--log-level trace|debug|info|warn|error`.

## Conventions to match

- C++17, 2-space indent, `Cadly::Warnings` baseline is strict (`-Wall -Wextra
  -Wpedantic -Wshadow` …; OCCT deprecations silenced). Don't introduce warnings.
- Comments here explain *why*, often at length, especially around the GL/Qt
  boundary and OCCT quirks. Match that density when touching those areas.

## Commit messages

Follow **Conventional Commits**. The subject line is `type(scope): summary`:

- **type** — one of:
  - `feat` — a new user-visible feature
  - `fix` — a bug fix
  - `docs` — documentation only (incl. this file)
  - `refactor` — code change with no behaviour change
  - `perf` — a performance improvement
  - `test` — adding or adjusting tests
  - `build` — build system or dependencies (CMake, vcpkg, OCCT/Qt wiring)
  - `chore` — tooling/maintenance with no production-code change
- **scope** *(optional)* — the affected module: `scene`, `cad`, `renderer`,
  `renderer_gl`, `ui`, `app`, `platform`, or `shaders`.
- **summary** — imperative mood, lowercase, no trailing period, ≤ ~72 chars
  ("add", not "added"/"adds"). State the user-visible change, not the mechanics.

Rules:

- One logical change per commit (e.g. don't mix a `fix` and unrelated `docs`).
- Optional body, wrapped at ~72 columns, explains the *why* for non-obvious
  decisions — match the density of the existing log.
- Breaking changes: add `!` before the colon (`feat!: …`) or a
  `BREAKING CHANGE:` footer.
- Commit/push only when asked.

Examples:

```
fix: stop panels from tearing off into floating windows
feat(cad): poll IProgressSink during STEP transfer
docs: add CLAUDE.md repository guide
```

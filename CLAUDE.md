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
(`vcpkg.json`) for Windows/portable builds.

```bash
cmake --preset linux-release          # configure (RelWithDebInfo)
cmake --build --preset linux-release  # build -> build/linux-release/bin/
ctest   --preset linux-release        # run smoke tests

build/linux-release/bin/cadly [file.step]   # GUI; optional file opens at startup
build/linux-release/bin/cad_import_cli f.step  # headless import, prints geo stats
```

Presets: `linux-debug`, `linux-release`, `linux-qt68-{debug,release}` (Qt 6.8
from `~/Qt/6.8.3/gcc_64`, enables the qlementine style), `linux-vcpkg-debug`,
`windows-msvc-{debug,release}` (VS solution), `windows-ninja-{debug,release}`
(single-config Ninja; expects MSVC in the environment, i.e. a VS dev prompt —
what CI uses, since it can't pin a VS-year generator to a rotating runner
image). `windows-ninja-release` pins the release-only `x64-windows-release`
triplet — a release build never links the debug deps, so vcpkg skips building
debug Qt/OCCT (half the cold dependency build). The debug and VS presets keep
stock `x64-windows`; the VS one because its multi-config generator can still
build a Debug config. CI (`.github/workflows/ci.yml`) builds, tests, and
packages on Linux + Windows; the smoke test and a headless STEP import are
the gates.

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

- Mouse: **right-drag orbits, middle-drag pans**, left is reserved for picking
  (not yet implemented, passed through). Wheel zoom anchors on the point under
  the cursor. Orbit uses a quaternion camera around a pluggable
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
  unwinds before zero-chrome). Isolate persists per tab via
  `DocumentState::isolate_node`; the flags live in the scene's nodes but the
  sidebar wipes them on every scene handover and the shell re-applies.
- Shortcuts: `F` fit, `W` wireframe, `H` hidden line, `E` edges, `T` triangle mesh, `P`
  perspective toggle (ortho is default for CAD). Standard views `1`-`7`
  (Front/Back/Right/Left/Top/Bottom/Iso, Blender-style numbering). `⌃.` toggles
  zero-chrome (all panels hidden; Esc restores); `⌥⌘O` opens with the import
  pre-flight; `Ctrl+W` closes the active document tab.
- Surface modes are visibly exclusive in the toolbar
  `Shaded|Hidden Line|Wireframe` `SegmentedControl`. Edges/Mesh chips apply to
  Shaded and are **disabled-but-remembered** in the other two modes. Hidden
  Line's chevron menu holds "Dimmed Hidden Lines" (`show_hidden_edges`);
  its checked state is the preference itself, so it is only ever
  enabled/disabled with the mode, never force-cleared.
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
- Dev aid: `cadly --screenshot <png> [--demo hiddenline|wireframe|light|views|getinfo|shadedmenu|hiddenmenu|zerochrome|highlight|isolate]`
  drives a UI state and grabs it headlessly (used to verify the shell without an
  input-injection tool). `--demo hiddenline-orbit:<yaw>,<pitch>[,persp]` (and
  the `wireframe-orbit:` twin) screenshots a line mode at an exact arbitrary
  orientation — the silhouette pass is view-dependent, so regressions hide at
  in-between azimuths the seven standard views never hit.

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

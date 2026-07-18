# Cadly “Graphite” — UI redesign concept

*2026-06-11. Companion to [`prototype.html`](prototype.html) — open it in any
browser (no server needed, fully self-contained). This document records the
design direction, what the prototype demonstrates, the honest Qt mapping, and
the open questions to settle before migration.*

> **Status (2026-06-13): implemented in Qt.** The Graphite shell now ships in
> `src/ui` + `src/app`, replacing the `QDockWidget` shell. Built clean on both
> `linux-release` (Fusion fallback) and `linux-qt68-release` (qlementine), both
> smoke tests pass, `cad_import_cli` unchanged. Verified on WSLg via the
> `--screenshot`/`--demo` dev hook: dark + light shells, runtime theme swap,
> wireframe (segmented control + disabled-but-remembered chips), Views popover
> with keycap tiles, pinnable Get Info popover, non-modal import with
> success/failure capsule states, zero-chrome (`⌃.`) with the renderer-drawn
> axes triad + scale bar surviving. The six open questions below were resolved
> with the recommended defaults (always-visible menubar; dark **and** light from
> day one; HUD cluster kept; per-node visibility eyes in scope; pinnable Get
> Info card; badge-dot on success / auto-open log on failure). What the mock
> over-promised still holds: native title bar (no frameless CSD) and flat
> tinted panels (no backdrop blur) — `setAlphaBufferSize(0)` is load-bearing.
> One deviation from the sketch: theme tokens live in a `ui::ThemeTokens`
> struct read by the custom widgets, exactly as the "honest Qt mapping" below
> proposed.

> **Update (2026-07-17): multi-document tabs supersede the document capsule.**
> The historical capsule references below describe the original Graphite
> proposal. The current shell shows each open file in a closable tab below the
> toolbar; import progress appears in that tab and the status bar. The display
> control is now `Shaded | Hidden Line | Wireframe`; `H` selects Hidden Line.
> `Edges` and `Triangle Mesh` now live in a split menu on `Shaded`: both are
> subordinate overlays that are only legal in that surface mode, so keeping
> them as peer-level toolbar chips advertised impossible combinations. The
> split menu preserves their independent checked state and E/T shortcuts while
> matching the prototype's compact, anchored-popover visual language. This
> supersedes the historical chip references below.
> A visual-parity pass also replaced the stock Fusion document tabs, aligned
> stock controls to the 24–28px / 6px Graphite geometry, moved sidebar search
> into the panel header, restored part glyphs, and added restrained 120–140ms
> hover/segment transitions. The native title bar remains intentionally native.

## How this direction was chosen

Three competing macOS-inspired concepts were drafted against a full inventory
of the current UI and scored by three judges (a macOS HIG reviewer, a CAD/
inspection workflow reviewer, a Qt Widgets engineer) on HIG fidelity, CAD
workflow fit, Qt feasibility, and freshness:

| Concept | Model | Scores (2 judges, /40) |
|---|---|---|
| **Graphite** — fixed pro-app regions, Xcode-calibre | Xcode / Final Cut | **33, 32 — winner** |
| Floating Canvas — all chrome floats over the GL view | Shapr3D / Freeform | 25, 24 |
| Preview-calm — hidden menubar, popovers, sheets | Preview / Maps | 29, 31 |

Graphite won on the things an inspection tool lives on: model tree **and**
properties visible at once, zero occlusion of geometry, dark-room legibility,
honest Qt feasibility. Its weakness — answering “too rigid” with another fixed
grid — is patched by grafting the best de-rigidifying elements of the losers:

- **Zero-chrome mode** (`⌃.`) — all panels collapse with layout memory (Floating Canvas).
  The toggle is mirrored in the in-viewport HUD (swapping to a “fullscreen-exit”
  glyph) so there is always a visible way back; Esc restores too.
- **Non-modal import** — previous scene stays orbit-able during import; only
  Open/drop are disabled (the old app-modal QProgressDialog dies).
- **Light theme** as a first-class token table, viewport gradient pushed
  through `DisplayMode.background_top/bottom` (Preview-calm’s mechanism).
- **Pinnable “Get Info” popover** from tree rows — a second properties surface
  for side-by-side part comparison.
- **Visual Views popover** with cube-face tiles + keycap badges (teaches `1–7`).
- **Badge-dot notifications** — success badges the Diagnostics toggle; only
  *failure* auto-opens the log. No focus stealing.

Explicitly rejected: frameless window with fake traffic lights, backdrop blur
as a requirement, hidden menubar, floating tree/inspector cards, and an
exclusive 3-segment display pill (it can’t represent the legal Edges+Mesh
coexistence).

## The layout (five fixed regions)

```
┌────────────────────────────────────────────────────────────────┐
│ ◉◉◉  ⫿  [⌂ Open ▾]      ⟦ flange_bracket.step ⟧      Shaded|Wire│ 52px unified
│                          ⟦ 12 nodes · 3.4k tris ⟧   [E][T] ◫ ⊞ ⫿│ toolbar
├──────────┬──────────────────────────────────────────┬──────────┤
│ MODEL  🔍│                                  ┌─────┐ │ ⓘ ☰ ⤓    │
│ ▾ flange │                                  │views│ │ inspector │
│   base…42│          GL viewport             │fit ⊥│ │ Props /   │
│   hub_b… │   (gradient, scale bar,          └─────┘ │ Display / │
│  ▸ frame │    axes triad — renderer-drawn)          │ Import    │
│  ▸ hardw…│                                          │           │
│          ├──────────────────────────────────────────┤           │
│          │ Summary | Log          (diagnostics)     │           │
├──────────┴──────────────────────────────────────────┴──────────┤
│ flange_bracket.step      MSAA 4× · 12 nodes · 3.4k tris · idle │ 26px status
└────────────────────────────────────────────────────────────────┘
```

- **Toolbar (52px)** — sidebar toggle · Open split-button (recents in the
  chevron) · center **document capsule** (filename + live stats; becomes the
  import progress capsule; click → full-path popover) · display segmented
  control `Shaded|Wireframe` + `Edges`/`Mesh` chips · projection / Views /
  Fit · appearance · panel toggles · zero-chrome.
- **Sidebar (260px)** — source-list model tree: 28px rows, disclosure
  triangles, right-aligned triangle counts, hover eye (⌥-click solos), hover
  ⓘ → Get Info popover, filter field.
- **Viewport** — untouched GL contract. Renderer-drawn scale bar (fixed
  length, live label) and a revived `show_axes` triad. Small floating view
  cluster top-right (mirrors toolbar actions). Welcome card when empty.
- **Diagnostics strip** (center column only, Xcode debug-area style) —
  `Summary | Log` segmented; closed by default; badge-dot on success,
  auto-open on failure.
- **Inspector (300px)** — three icon tabs: **Properties** (selection),
  **Display** (edge intensity, MSAA, scale bar, axes, projection — exposes
  the previously code-only `DisplayMode` knobs), **Import** (the entire
  ImportOptionsDialog relocated as a persistent panel + “Review options
  before each import” + Re-import). Display and Import each carry a
  right-aligned “Reset to Defaults” text button (accent `.txtbtn`) that
  restores the struct/backend defaults in one click; the review-before-import
  checkbox is a workflow preference and survives the reset.

### Display-mode model

The segmented control models the real exclusivity instead of hiding it:
`Shaded ↔ Wireframe` are exclusive; `Edges`/`Mesh` chips apply to Shaded and
are **disabled-but-remembered** while Wireframe is active (replaces the
QSignalBlocker silent-uncheck dance — same rules, now visible). `W/E/T/P`
keep their meanings; `1–7` views and `F` unchanged.

### Import flow

Open (toolbar, ⌘O, recents, drag-drop, CLI) imports **immediately** with
persisted options — the every-open modal dialog dies. Pre-flight is opt-in
(“Review options…” checkbox, `⌥⌘O`, or File ▸ Open with Options…) as an
anchored popover. Progress lives in the capsule (reusing the existing 33 ms
`GuiProgressSink` poll + cancel flag verbatim). Failure turns the capsule red
and auto-opens the log; success green-flashes and badges the strip toggle.

## Visual tokens (dark / light)

| Token | Dark | Light |
|---|---|---|
| Accent (one blue, unifies #3574F0 vs #5086FF) | `#5086FF` | `#3D6FE0` |
| Toolbar / sidebar / inspector | `#26282D` / `#212328` / `#232529` | `#F2F3F5` / `#ECEDF0` / `#F6F7F9` |
| Strip header / log / status | `#202226` / `#17181C` / `#202226` | `#ECEDF0` / `#FAFBFC` / `#ECEDF0` |
| Text 1/2/3 | `#E6E8EC` / `#A0A6B0` / `#6E747E` | `#1E2126` / `#5A616C` / `#9AA0AA` |
| Hairlines between regions / inside panels | `rgba(0,0,0,.45)` / `rgba(255,255,255,.07)` | `rgba(0,0,0,.16)` / `rgba(0,0,0,.10)` |
| Viewport gradient (`DisplayMode`) | `#ACB0B7→#80838A` (unchanged) | `#DADDE2→#ABAFB7` |
| Status colors | error `#E96B72` · warn `#FBC064` · ok `#2BB5A0` · info `#1BA8D5` (from `themes/dark.json`) | darkened variants |

Type: system stack (SF Pro on macOS; qlementine bundles Inter; Fusion uses the
system font). 13px body, 11px secondary, 11px/600 uppercase section heads,
mono + `tabular-nums` for every measured value. Radii: 6px controls, 8px
menus/capsule, 10–12px cards. Spacing on a 4px grid. The viewport stays the
*lightest* surface on screen in dark mode — chrome sits below it in luminance
so the part pops (load-bearing relationship, per `Theme.cpp`).

## Feature-parity audit

Every current feature has a home: Open/Ctrl+O → toolbar split-button; recents
+ last-dir (wires the **dead** `app::RecentFiles` / `Settings::
last_open_directory`); model tree → sidebar (+ new visibility eyes); properties
→ inspector tab + Get Info popover; import diagnostics → strip (Summary/Log);
display modes incl. triangle-mesh debug → segmented + chips; MSAA / edge
intensity / scale bar / axes → Display tab (all previously code-only);
projection + standard views + fit → toolbar, HUD cluster, popover, keys;
import options → Import tab + pre-flight (healing/welding exist in
`ImportOptions` today but the current dialog never exposed them — the Import
tab surfaces both, with the backend defaults: healing on, welding off);
status info → capsule + status bar (now with frame-time readout); About/Quit/
Panels → menu bar (kept complete — macOS never hides the menubar).

**New behaviors introduced** (each is cheap but real scope): per-node
visibility (needs a scene/renderer flag — the one genuine engine touch),
window title shows the file, drag-and-drop opening, non-modal import,
display/import settings persistence, frame-time instrumentation.

## Honest Qt mapping (what the mock overpromises)

| Mock shows | Qt build ships | Notes |
|---|---|---|
| Toolbar merged with title bar, traffic lights | **Native OS title bar** + 52px custom-painted bar below | Frameless CSD is a multi-week cross-platform tax; not worth it. Window title carries the filename. |
| Backdrop blur “glass” (sidebar, popovers, HUD) | Flat tinted panels (~92% opaque fills over the GL view) | `setAlphaBufferSize(0)` is load-bearing; widget-level translucency over QOpenGLWidget **does** work (FBO composition), real blur does not. A renderer blur-behind pass is a documented future option. |
| macOS system menu bar | Regular in-window QMenuBar (Linux/Win) | Keeps the complete action mirror. |

Implementation sketch (Qt 6.8/qlementine first-class, Fusion 6.4 same layout):

1. **Shell rewire (M)** — central widget becomes `QVBoxLayout{ToolbarWidget,
   QSplitter{sidebar | QSplitter_v{viewport, strip} | inspector}}`. QDockWidgets,
   `saveState()` blob, and the `showEvent` un-float hack die; explicit QSettings
   keys replace them (schema-version bump). Panels menu = 3 plain checkable
   QActions shared with the toolbar toggles.
2. **Custom widget kit (M)** — one `SegmentedControl` (~300 lines, QPainter +
   QActionGroup), capsule, chips, splitter handles, tree item delegate (pill
   selection, count, eye hit-testing in `editorEvent`). All read a
   `ThemeTokens` struct (not QPalette) so Fusion 6.4 and qlementine 6.8 render
   identically.
3. **Popover framework (M)** — `Qt::Popup | FramelessWindowHint` base with
   anchor placement; opaque square-corner fallback where ARGB visuals are
   missing (X11/WSLg must be tested, not assumed).
4. **Import presentation (S)** — capsule reuses `GuiProgressSink` + 33 ms
   QTimer + `QFutureWatcher` verbatim; replace ApplicationModal with an
   explicit `importing_` guard (disable Open/drop only). Decide behaviors for
   the newly legal states: orbit-during-import, quit-during-import.
5. **Inspector (S–M)** — `QStackedWidget`; Import tab lifts the existing
   dialog internals + enablement logic unchanged.
6. **Engine touches (S each, scoped separately)** — per-node visible flag
   (scene + renderer respect), axes triad modeled on `draw_scale_bar`,
   scale-bar toggle, `QElapsedTimer` around `paintGL` for the frame-time
   readout (label it frame time — rendering is event-driven, “fps” would lie).
7. **Nearly free wires** — RecentFiles menu, last-open dir, window title,
   drag-drop (`setAcceptDrops`), display/import persistence, unify accent.

Total estimate: **~4–6 engineer-weeks** to mock parity on Qt 6.8; the custom
widget kit + popovers + shell rewire are the three big rocks. `scene`,
`cad`, `renderer` modules untouched except the visibility flag and two small
renderer_gl overlays.

## Risks to keep in view

- Stakeholders must sign off on the **flat-panel Qt rendering**, not the
  blurred mock (the prototype’s About box says this too).
- Dock muscle memory: panels become fixed-position (visibility/size only).
  Needs a release note; old layout blobs are ignored once.
- Defaults flip from ask-every-import to ask-never — the Review checkbox and
  `⌥⌘O` are the escape hatches; call it out in release notes.
- Quieter failure surface (capsule + auto-opened log instead of QMessageBox)
  must stay loud enough; auto-open is the safeguard.
- Custom-painted widgets need explicit QAccessible work and HiDPI hairline
  care under the strict warning set.

## Open questions for discussion

1. **Menubar on Linux/Windows** — keep the in-window QMenuBar always visible
   (recommended; it’s the complete mirror), or auto-hide behind Alt?
2. **Light theme scope** — ship dark-only first (gradient re-tune gate), or
   both from day one as the prototype shows?
3. **In-viewport HUD cluster** — keep (Views/Fit/Ortho mirrors in the canvas,
   top-right), or toolbar-only for zero occlusion? The prototype keeps it, and
   it now also hosts the zero-chrome toggle — the only chrome left on screen in
   that mode — which settles it toward keep.
4. **Per-node visibility eyes** — in scope for the UI milestone (it touches
   scene/renderer), or deferred behind a flag?
5. **Pinned Get Info card** — enough for compare workflows, or should the
   inspector Properties tab support a split/locked mode instead?
6. Default strip state after success: badge-dot only (current choice) vs
   auto-open Summary?

## Files

- `prototype.html` — the interactive concept (this is a *design artifact*;
  the JS “renderer” is a stand-in that mimics what GLRenderer draws).
- `design-notes.md` — this document.

# Cadly

> A simple CAD model import and viewing tool — open a STEP/IGES file and inspect it.

[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)

[中文文档](README.zh-CN.md)

Cadly is a native C++17 desktop CAD **viewer** (not an editor). It imports
STEP/IGES through Open CASCADE (OCCT), triangulates the B-Rep, and draws it with
a hand-written real-time renderer (OpenGL 4.1 core; the interface is built to
take a Vulkan backend later). Qt 6 Widgets provides the shell. Materials are PBR
metallic-roughness with image-based lighting, tuned for industrial inspection.

## Features

- **Import** STEP and IGES via OCCT CAF readers — colors, names, and assembly
  hierarchy preserved, with geometry-only fallback readers.
- **Non-modal import** on a worker thread: progress and cancel live in the
  document tab, and the previous scene stays interactive throughout.
- **Display modes** — shaded, shaded with edges, hidden line (with optional
  dimmed hidden lines), wireframe with an analytical LOD edge ladder, and a
  triangle-mesh debug overlay. View-dependent silhouette contours are generated
  on the GPU for the line modes.
- **CAD-style navigation** — right-drag orbits, middle-drag pans, cursor-anchored
  wheel zoom, orthographic by default, standard views on `1`–`7`.
- **Assembly tree** with selection highlight and double-click isolate (ghost or
  hide the rest).
- **Tabbed documents**, an inspector with persistent import options (meshing
  deflection, healing, welding), dark/light themes, and a zero-chrome mode.
- **Localized UI** — English and Simplified Chinese.
- **Headless CLI** (`cad_import_cli`) for validating imports without a display.

## Build & run

CMake presets (Ninja). System Qt 6 + OCCT on Linux; a vcpkg manifest
(`vcpkg.json`) for Windows/portable builds.

```bash
cmake --preset linux-release          # configure (RelWithDebInfo)
cmake --build --preset linux-release  # build -> build/linux-release/bin/
ctest   --preset linux-release        # smoke tests

build/linux-release/bin/cadly [file.step]        # GUI; optional file opens at startup
build/linux-release/bin/cad_import_cli file.step # headless import, prints geo stats
```

Other presets: `linux-debug`, `linux-qt68-{debug,release}` (Qt 6.8 with the
qlementine style), `linux-vcpkg-debug`, `windows-msvc-{debug,release}` (VS
solution), `windows-ninja-{debug,release}` (single-config Ninja, expects MSVC in
the environment). CI builds, tests, and packages on Linux and Windows.

### Requirements

- C++17 compiler, CMake ≥ 3.24, Ninja
- Qt 6 (Widgets, OpenGL, Concurrent, Svg; LinguistTools optional)
- Open CASCADE 7.6+
- glm, spdlog, fmt
- A GPU/driver with OpenGL 4.1 core

## Architecture

One CMake target per module under `src/`, exported as `Cadly::<Name>`:

| Module | Target | Role |
|---|---|---|
| `platform` | `Cadly::Platform` | logging, asset/path lookup |
| `scene` | `Cadly::Scene` | canonical model — glm only, no Qt/OCCT/GL |
| `renderer` | `Cadly::Renderer` | `IRenderer` / `RenderTypes` interface |
| `renderer_gl` | `Cadly::RendererGL` | OpenGL 4.1 backend, Qt-free |
| `cad` | `Cadly::Cad` | importers — the only target linking OCCT |
| `ui` | `Cadly::Ui` | widgets, viewport host |
| `app` | `cadly` | entry point, settings, recent files |

Data flow: file → `ImporterRegistry::select()` → OCCT STEP/IGES importer →
`BRepMesh_IncrementalMesh` → `scene::Mesh` → `scene::Scene` →
`IRenderer::attach_scene()` → `render()`.

`docs/cad-viewer-plan.md` holds the original design plan and milestone map;
`docs/ui-redesign/` documents the shell layout. `CLAUDE.md` describes the
invariants to preserve when changing the code.

## License

Licensed under the Apache License, Version 2.0 — see [LICENSE](LICENSE).

Cadly links Open CASCADE (LGPL-2.1 with an exception) and Qt 6 (LGPL-3.0); those
components remain under their own licenses.

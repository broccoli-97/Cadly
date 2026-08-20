# Cadly

Open a STEP or IGES file and look at it. That's the whole idea.

[![CI](https://github.com/broccoli-97/Cadly/actions/workflows/ci.yml/badge.svg)](../../actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)
[中文文档](README.zh-CN.md)

Cadly is a native C++17 desktop CAD viewer — not an editor. It reads STEP/IGES
through Open CASCADE, triangulates the B-Rep, and draws the result with a
hand-written real-time renderer (OpenGL 4.1 core today; the interface leaves
room for a Vulkan backend). The shell is Qt 6 Widgets. Shading is PBR
metallic-roughness with image-based lighting, tuned so machined surfaces read
clearly rather than look cinematic.

![Cadly viewing an RC buggy suspension assembly](docs/images/shell-overview.png)

<table>
<tr>
<td width="50%"><img src="docs/images/shaded-zero-chrome.png" alt="Shaded view with all panels hidden"></td>
<td width="50%"><img src="docs/images/wireframe.png" alt="Wireframe view with GPU silhouette contours"></td>
</tr>
<tr>
<td align="center"><sub>Zero-chrome mode — panels out of the way (<code>⌃.</code>)</sub></td>
<td align="center"><sub>Wireframe, with silhouettes generated per frame on the GPU</sub></td>
</tr>
</table>

## What it does

Import goes through OCCT's CAF readers, so colors, part names, and the assembly
hierarchy survive the trip; plain geometry-only readers act as the fallback when
a file has no product structure. The work happens on a background thread and is
never modal — progress and cancel sit inside the document tab, and whatever you
were already looking at stays interactive.

Five ways to look at a model: shaded, shaded with edges, hidden line (with
dimmed occluded lines as an option), wireframe with an analytical LOD edge
ladder that refines as you zoom, and a triangle-mesh overlay for debugging the
tessellation. Both line modes get view-dependent silhouette contours from a
geometry-shader pass, so a cylinder still reads as a cylinder while you orbit.

Navigation follows CAD convention: right-drag orbits, middle-drag pans, the
wheel zooms toward the cursor, projection is orthographic by default, and
`1`–`7` snap to standard views. Clicking a part in the assembly tree highlights
it in the viewport; double-clicking isolates it and ghosts (or hides) the rest.

Around that: tabbed documents, an inspector holding persistent import options
(meshing deflection, healing, welding), dark and light themes, English and
Simplified Chinese, and `cad_import_cli` for checking an import without a
display.

## Build & run

CMake presets, Ninja. On Linux the build uses the system Qt 6 and OCCT; Windows
and portable builds go through the vcpkg manifest (`vcpkg.json`).

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
the environment).

You need a C++17 compiler, CMake ≥ 3.24, Ninja, Qt 6 (Widgets, OpenGL,
Concurrent, Svg; LinguistTools optional), Open CASCADE 7.6+, glm, spdlog, fmt,
and a driver with OpenGL 4.1 core.

## Prebuilt packages

Every push to `main` builds and tests on Linux and Windows and uploads runnable
packages as workflow artifacts. Tagged versions are published on the
[Releases](../../releases) page: a portable Linux x64 tarball and a
self-contained Windows x64 zip, neither of which needs Qt or OCCT installed.

Cutting a release is a tag push:

```bash
git tag v0.1.0 && git push origin v0.1.0
```

The `Release` workflow (`.github/workflows/release.yml`) builds both packages
from that tag through the same pipeline CI runs, then opens a draft release
with them attached and generated notes — edit the notes and publish when it
looks right. A manual run from the Actions tab against an existing tag rebuilds
that release's assets in place.

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

The boundaries are the point: `scene` stays free of Qt, OCCT, and any graphics
API so it can sit between any importer and any backend, and `renderer_gl` takes
a GL function loader instead of linking Qt.

`docs/cad-viewer-plan.md` holds the original design plan and milestone map,
`docs/ui-redesign/` documents the shell layout, and `CLAUDE.md` lists the
invariants worth preserving when changing the code.

## License

Apache License 2.0 — see [LICENSE](LICENSE). Cadly links Open CASCADE
(LGPL-2.1 with an exception) and Qt 6 (LGPL-3.0); those components remain under
their own licenses.

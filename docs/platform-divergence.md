# Platform divergence

Where Cadly deliberately behaves differently per platform, and why. Update
this file whenever a `Q_OS_*` / `#ifdef` branch or a per-platform preset is
added or changed.

## Keyboard bindings

Qt maps `Qt::CTRL` to the **Command** key on macOS (and `Qt::META` to the
physical Control key), so most shortcuts diverge automatically and correctly.
Only Zero Chrome needs an explicit per-platform binding.

| Action            | Linux / Windows | macOS  | Source |
|-------------------|-----------------|--------|--------|
| Open              | Ctrl+O          | ⌘O     | `QKeySequence::Open` (standard key) |
| Open with Options | Ctrl+Alt+O      | ⌥⌘O    | `Qt::CTRL \| Qt::ALT \| Qt::Key_O`, auto-mapped |
| Close Tab         | Ctrl+W          | ⌘W     | `QKeySequence::Close` (standard key) |
| Quit              | Ctrl+Q          | ⌘Q     | `QKeySequence::Quit` (standard key) |
| Zero Chrome       | Ctrl+.          | **⌃.** | explicit `#ifdef` in `MainWindow::build_actions` |
| Fit, display modes, views | `F W H E T P` `1`–`7` | same | unmodified keys, no divergence |
| Exit isolate / restore chrome | Esc | same | |

Zero Chrome is the one case the automatic mapping gets wrong: `Qt::CTRL |
Qt::Key_Period` would surface as **⌘.**, which macOS reserves as the
system-wide Cancel idiom (`QKeySequence::Cancel` — what users press to abort
a dialog or a long operation). The macOS branch binds `Qt::META |
Qt::Key_Period` instead, which renders as ⌃. — the combination CLAUDE.md
documents. Menus render shortcuts with the native glyphs via
`QKeySequence::NativeText`, so no display-side handling is needed.

## Menu bar

The in-window `QMenuBar` is promoted to the system menu bar on macOS
automatically. Qt's text heuristics relocate "Quit" and "About…" into the
application menu; no `setMenuRole` overrides are set, so keep those action
titles heuristic-friendly (don't rename "Quit" to e.g. "Exit").

## File-path identity

Duplicate-document detection (`MainWindow::find_document`) compares canonical
paths **case-insensitively on Windows and macOS** (NTFS and default
APFS/HFS+ volumes are case-insensitive), case-sensitively elsewhere. A
case-sensitive APFS volume can in principle hold two files differing only by
case; we accept treating them as one document — the common case is the same
file typed with different casing.

## OpenGL

- The renderer requests GL 4.1 core everywhere — chosen as the floor
  *because* it is macOS's ceiling. Nothing above 4.1 may be used.
- `<GL/glcorearb.h>` is vendored in `src/renderer_gl/third_party/khronos/`
  because macOS's OpenGL.framework does not ship it (and system copies vary
  elsewhere). No platform GL library is linked; entry points resolve at
  runtime through the host-supplied `GLLoadProc`.
- macOS core profile clamps `glLineWidth` to 1.0. The 1.2 px edge / 1.8 px
  selection line widths silently render at 1 px there; a shader-based line
  expansion would be needed for parity (known cosmetic divergence).
- The silhouette pass uses geometry shaders (GL 3.2 core, legal on macOS
  4.1) but Apple's GS driver path is slow and lightly tested — verify with
  `--demo hiddenline-orbit:<yaw>,<pitch>` after renderer changes.

## Window-system quirks

- `main.cpp` forces `QT_QPA_PLATFORM=xcb` under WSLg only (`Q_OS_LINUX`).
- MSVC gets `NOMINMAX` and `/W4`; Clang/AppleClang/GCC share the `-Wall
  -Wextra -Wpedantic -Wshadow` baseline.

## Dependencies & packaging

| | Linux | Windows | macOS |
|---|---|---|---|
| Dependency source | apt (system Qt 6.4/OCCT 7.6) or vcpkg | vcpkg manifest | Homebrew (`scripts/setup-macos.sh`) |
| Qt style | Fusion fallback (Qt < 6.8) unless `linux-qt68-*` | qlementine (vcpkg Qt 6.11) | qlementine (Homebrew Qt ≥ 6.8) |
| App artifact | portable tarball (`patchelf`, `$ORIGIN` rpaths) | self-contained dir (applocal DLLs + plugin copy) | `.dmg` with self-contained, ad-hoc-signed `Cadly.app` (`packaging/macos/package-app.sh`: macdeployqt + rpath/install-name rewrite; assets in `Contents/Resources`) |
| Presets | `linux-*` | `windows-*` | `macos-{debug,release}` |

On macOS the `cadly` target builds as `bin/cadly.app`; the GUI binary lives
at `bin/cadly.app/Contents/MacOS/cadly` (dev builds still find shaders via
the `CADLY_SOURCE_ROOT` fallback). `cad_import_cli` stays a plain binary at
`bin/cad_import_cli` on all platforms.

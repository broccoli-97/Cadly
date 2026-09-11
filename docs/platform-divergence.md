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

Viewport navigation is likewise shared: the selected navigation scheme
(Cadly, Blender, Rhino, Fusion 360, Maya — Preferences ▸ Navigation) uses the
same binding table on every platform, with modifiers rendered natively
(⌥-drag / ⌥⌘-drag on macOS, Alt / Alt+Ctrl elsewhere). The two trackpad
rows — Alt+left orbit, Alt+Ctrl+left pan — exist in every scheme chiefly
for trackpads: no middle button, and a held two-finger click is a poor
orbit.

Input normalization and gesture ownership live in `Cadly::Input` and its
`Cadly::InputQt` adapter, not in the camera or settings window. `Primary` is
Qt's logical Control modifier (Command on macOS); `Secondary` is Qt's Meta
modifier (physical Control on macOS). No second platform-specific swap occurs.
Logical pointer coordinates, late-modifier trackpad drags, exact modifier
matching, and natural-scroll delta handling are covered by independent tests.
The shared INI paths `display/navigation_scheme` and `display/orbit_style`
remain unchanged. See [input-architecture.md](input-architecture.md) for the
module boundaries, extension points, and regression matrix.

Zero Chrome is the one case the automatic mapping gets wrong: `Qt::CTRL |
Qt::Key_Period` would surface as **⌘.**, which macOS reserves as the
system-wide Cancel idiom (`QKeySequence::Cancel` — what users press to abort
a dialog or a long operation). The macOS branch binds `Qt::META |
Qt::Key_Period` instead, which renders as ⌃. — the combination CLAUDE.md
documents. Menus render shortcuts with the native glyphs via
`QKeySequence::NativeText`, so no display-side handling is needed.

## Preferences

One `PreferencesDialog` everywhere; only its entry point diverges. The
action's `PreferencesRole` moves it into the macOS app menu as
"Settings… ⌘," automatically; on Linux and Windows it stays at
File ▸ Preferences…. `QKeySequence::Preferences` supplies ⌘, on macOS and
Ctrl+, where the platform theme defines one — on Windows it defines none,
so the menu item is the only route there.

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
- Windows builds define `NOMINMAX` under both MSVC and MinGW. MSVC uses `/W4`;
  Clang/AppleClang/GCC share the `-Wall -Wextra -Wpedantic -Wshadow` baseline.

## Dependencies & packaging

| | Linux | Windows | macOS |
|---|---|---|---|
| Dependency source | apt (system Qt 6.4/OCCT 7.6) or vcpkg | MSYS2 UCRT64 binary packages (`scripts/setup-windows.sh`) | Homebrew (`scripts/setup-macos.sh`) |
| Qt style | Fusion fallback (Qt < 6.8) unless `linux-qt68-*` | qlementine (MSYS2 Qt ≥ 6.8) | qlementine (Homebrew Qt ≥ 6.8) |
| App artifact | portable tarball (`patchelf`, `$ORIGIN` rpaths) | self-contained dir (`packaging/windows/package-portable.cmake`: windeployqt + CMake runtime DLL scan) | `.dmg` with self-contained, ad-hoc-signed `Cadly.app` (`packaging/macos/package-app.sh`: macdeployqt + rpath/install-name rewrite; assets in `Contents/Resources`) |
| Presets | `linux-*` | `windows-msys2-{debug,release}` | `macos-{debug,release}` |

Windows CI installs current binary packages with pacman, including GCC, Qt,
OCCT, and QtTest. It has no vcpkg baseline, NuGet feed, or dependency build
cache to maintain. The `windows-msvc-*` and `windows-ninja-*` presets remain
optional vcpkg development configurations; their libraries cannot be mixed
with MinGW/UCRT64 libraries. Windows release packages bundle the GCC runtime
and do not require MSYS2 on the user's machine. CI checks the packaged GUI
and STEP importer with MSYS2 removed from `PATH`.

On macOS the `cadly` target builds as `bin/cadly.app`; the GUI binary lives
at `bin/cadly.app/Contents/MacOS/cadly` (dev builds still find shaders via
the `CADLY_SOURCE_ROOT` fallback). `cad_import_cli` stays a plain binary at
`bin/cad_import_cli` on all platforms.

GUI tests select Qt's offscreen platform and resolve its plugin directory
from `Qt6::QOffscreenIntegrationPlugin`, including local vcpkg builds whose
applocal deployment copies linked DLLs without platform plugins. A missing
platform plugin can show a modal error dialog on a Windows runner without a
console. Every test has a 60-second timeout, and Windows CI streams verbose
test output to expose startup failures.

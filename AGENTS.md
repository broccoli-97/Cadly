# Repository Guidelines

## Project Structure & Module Organization

Cadly is a native C++17 CAD viewer built with CMake. Modules use public headers
in `src/<module>/include/cadly/<module>/` and implementations in
`src/<module>/src/`. The modules are `platform`, `scene`, `renderer`,
`renderer_gl`, `cad`, `ui`, and `app`. Runtime GLSL and theme assets live in
`shaders/glsl/` and `themes/`; tests and the public STEP fixture are in `tests/`
and `test_files/`. Design records and the UI prototype are under `docs/`.

Respect dependency boundaries: `scene` remains independent of Qt, OCCT, and
graphics APIs; only `cad` links OCCT; `renderer_gl` is Qt-free. Qt owns windows,
input, and context setup. Viewport geometry, labels, HUDs, and overlays belong
to the renderer, never `QPainter`.

## Build, Test, and Development Commands

- `cmake --preset linux-debug` configures Ninja in `build/linux-debug/`.
- `cmake --build --preset linux-debug` builds the GUI, import CLI, and tests.
- `ctest --preset linux-debug` runs CTest with failure output.
- `build/linux-debug/bin/cadly [file.step]` launches the viewer.
- `build/linux-debug/bin/cad_import_cli test_files/as1-ug-214.stp` exercises
  OCCT import and tessellation without a display or GL context.

Use `linux-release` for RelWithDebInfo, `linux-qt68-*` with local Qt 6.8.3, and
`linux-vcpkg-debug` with `VCPKG_ROOT`. Windows contributors run
`bash scripts/setup-windows.sh` in MSYS2 UCRT64 and use `windows-msys2-*`;
`windows-ninja-*` and `windows-msvc-*` remain optional vcpkg presets.
macOS contributors run `scripts/setup-macos.sh` (Homebrew deps) and use
`macos-{debug,release}`; the GUI binary is
`bin/cadly.app/Contents/MacOS/cadly`, and deliberate
per-platform differences are recorded in `docs/platform-divergence.md`. CI
builds, tests, imports the fixture, and packages on all three platforms.

## Coding Style & Naming Conventions

Use C++17, 2-space indentation, and the surrounding brace style. Public APIs
use `cadly::<module>` namespaces; CMake aliases use `Cadly::<Name>`. No formatter
is enforced, so keep diffs consistent with nearby code. Add comments only for
non-obvious Qt, OCCT, or rendering behavior. Do not introduce warnings under
the shared `Cadly::Warnings` baseline (`-Wall -Wextra -Wpedantic -Wshadow` and
related checks; `/W4` on MSVC).

## Testing Guidelines

Add focused CTest executables in `tests/` and register names such as
`cadly_smoke` in `tests/CMakeLists.txt`. Run relevant debug and release presets
before submission. Importer changes also require `cad_import_cli` checks against
representative STEP or IGES inputs; do not commit private CAD files.

## Commit & Pull Request Guidelines

Follow Conventional Commits found in history: `type(scope): imperative summary`,
for example `feat(ui): replace dock shell with graphite layout`. Use lowercase,
no trailing period, and types such as `feat`, `fix`, `test`, `docs`, or `build`.
Non-trivial commits must also include a body that explains the motivation,
user-visible behavior, and validation performed; do not use a subject-only
commit message for feature work or bug fixes.
PRs should explain user-visible behavior and affected modules, link issues,
report exact test commands, and include screenshots for UI or viewport changes.

## Configuration & Assets

Do not commit build output or machine-specific paths. `CADLY_ASSET_ROOT` may
override runtime shader/theme lookup locally; keep it out of checked-in config.

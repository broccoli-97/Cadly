# Repository Guidelines

## Project Structure & Module Organization

Cadly is a native C++17 CAD viewer built with CMake. Source modules live under
`src/`, with public headers in `src/<module>/include/cadly/<module>/` and
implementation files in `src/<module>/src/`. Key modules are `platform`,
`scene`, `renderer`, `renderer_gl`, `cad`, `ui`, and `app`. GLSL runtime assets
are in `shaders/glsl/`. Smoke tests are in `tests/`, and design notes are in
`docs/`. Keep `scene` independent of Qt, OCCT, and OpenGL; keep OCCT usage in
`cad`; keep Qt wiring out of `renderer_gl`. Qt is only the GUI/input/context
host: do not draw viewport content, labels, HUDs, scale bars, or render
overlays with Qt/QPainter. Viewport visuals belong in `renderer`/`renderer_gl`
or renderer-owned assets.

## Build, Test, and Development Commands

- `cmake --preset linux-debug` configures a Debug Ninja build in
  `build/linux-debug/`.
- `cmake --preset linux-release` configures a RelWithDebInfo build.
- `cmake --build --preset linux-debug` builds all enabled targets.
- `ctest --preset linux-debug` runs the CTest smoke suite with failure output.
- `build/linux-debug/bin/cadly [file.step]` runs the GUI viewer.
- `build/linux-debug/bin/cad_import_cli file.step` validates CAD import paths
  without requiring a GUI or GL context.

Use `linux-vcpkg-debug` when building through the vcpkg manifest, with
`VCPKG_ROOT` set.

## Coding Style & Naming Conventions

Use C++17, 2-space indentation, and existing brace/style patterns. Public APIs
use the `cadly::<module>` namespace and module-local CMake targets exposed as
`Cadly::<Name>`. Prefer focused comments that explain non-obvious renderer,
Qt, or OCCT behavior. The warnings baseline is strict (`-Wall -Wextra
-Wpedantic -Wshadow` on non-MSVC); do not introduce new warnings.

## Testing Guidelines

Tests are CTest-based. Add focused tests under `tests/` and register them in
`tests/CMakeLists.txt` with descriptive names such as `cadly_smoke`. Run
`ctest --preset linux-debug` before submitting changes. For importer work,
also run `cad_import_cli` against representative STEP or IGES files when
available.

## Commit & Pull Request Guidelines

Recent history follows Conventional Commits: `type(scope): summary`, for
example `perf(cad): tessellate all parts in one parallel meshing pass` or
`fix: stop panels from tearing off into floating windows`. Use imperative,
lowercase summaries with no trailing period. Common types include `feat`,
`fix`, `docs`, `refactor`, `perf`, `test`, `build`, and `chore`.

Pull requests should describe the user-visible change, mention affected modules,
link related issues, and include test results. UI changes should include a short
note or screenshot showing the affected view.

## Security & Configuration Tips

Do not commit local build output, machine-specific paths, or sample CAD files
that are private. Runtime shader lookup can use `CADLY_ASSET_ROOT`; keep that
as a local environment setting rather than checked-in configuration.

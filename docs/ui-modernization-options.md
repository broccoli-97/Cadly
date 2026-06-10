# UI modernization options for Cadly

*Research notes, 2026-06-10. Decision pending — nothing here is implemented yet.*

## Current state

Cadly applies **no styling at all** — no `setStyle`, no palette, no QSS. The only
hits in `src/app/src/main.cpp` and `src/ui/src/MainWindow.cpp` are an empty
`app.setWindowIcon(QIcon())` and an unused `<QStyle>` include. The app gets Qt's
stock platform style, which on Linux/WSLg is the default light Fusion look —
that's the dated appearance. Anything added is pure win.

**Constraint verified:** system Qt is **6.4.2** (matters for option 3).

## Options, ranked for a C++/CMake Qt Widgets app

### 1. Zero-dependency quick win: Fusion + custom `QPalette`

`QApplication::setStyle("fusion")` plus a hand-tuned dark (or refined light)
palette in `main.cpp` gives a clean, flat, modern-ish look in ~30 lines with no
new dependencies and no risk. Many shipping Qt apps do exactly this. It won't
restyle widget *shapes* (scrollbars, tabs stay Fusion-shaped), but it kills the
dated gray instantly. Works fine on Qt 6.4.

### 2. QSS theme sheets

- **[BreezeStyleSheets](https://github.com/Alexhuszagh/BreezeStyleSheets)** —
  the pick in this category. MIT, actively maintained, explicit C++/CMake/Qt6
  support via `FetchContent` (build the `.qrc` resource, call
  `qApp->setStyleSheet(...)`). Themes are defined by ~40 color keys in a config
  file, so a Cadly-branded variant (accent color, etc.) is possible; ships
  dark/light/auto. Needs QtSvg for widget icons (scrollbar arrows, checkmarks).
- **[QDarkStyleSheet](https://github.com/ColinDuquesnoy/QDarkStyleSheet)** —
  the most famous one, works from C++ (it's just a QSS + resources), but the
  last release was **Nov 2023** and the README warns Qt6 support "may still
  present instabilities." Treat as a reference, not a dependency.

**General QSS caveat:** once an app-wide stylesheet is set, Qt switches to its
internal "stylesheet style" — widget types the sheet doesn't cover fall back to
fairly plain rendering, and QSS can't do hover animations or smooth transitions.
It's a reskin, not a new style engine.

### 3. A real modern `QStyle`: qlementine

**[oclero/qlementine](https://github.com/oclero/qlementine)** — the most
genuinely modern option for Qt Widgets today. A full C++ `QStyle`
implementation (not QSS), MIT, actively maintained (v1.4.2, Jan 2026), with
light/dark themes defined in JSON, smooth animations, automatic icon
colorization, and extra widgets like a `Switch`. Applied with one
`QApplication::setStyle(new QlementineStyle)`; restyles everything coherently —
much closer to a "designed in 2025" feel than any QSS sheet.

**Catch:** the current release requires **Qt 6.8+**; system Qt is 6.4.2.
Paths around it:

- upgrade system Qt, or
- build via the existing vcpkg manifest path (can pull a newer Qt), or
- pin an older qlementine tag that still built against earlier Qt 6 (the
  [release notes](https://github.com/oclero/qlementine/releases) don't state
  when the requirement was bumped — needs a quick build test).

### 4. Icons — the other half of "looks dated"

- **[qlementine-icons](https://github.com/oclero/qlementine-icons)** — 350+ MIT
  SVG icons designed for desktop apps, pixel-perfect at 16×16, follows the
  Freedesktop naming scheme. Integrates via `FetchContent` +
  `QIcon::setThemeName("qlementine")`, then `QIcon::fromTheme(...)`. Requires
  only **Qt 5.15.2+/Qt 6**, so it works on 6.4 *today*, independently of the
  qlementine style.
- Alternatively, any generic SVG set (Lucide, Tabler, Material Symbols) in a
  `.qrc` works — but plain `QIcon` won't recolor monochrome SVGs for dark
  themes; that's exactly what the qlementine style's auto icon colorization
  solves, which is why the pair is nice together.

### 5. Worth a mention for a CAD tool: Advanced Docking System

**[Qt-Advanced-Docking-System](https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System)**
replaces stock `QDockWidget`s (model tree / properties / diagnostics docks)
with VS-Code-style tabbed, draggable, restorable docks — a big chunk of the
"modern tool" feel beyond colors. Bigger change though: the View ▸ Panels menu
logic is built around `QDockWidget::toggleViewAction()`.

## Recommendation

Two steps:

1. **Now:** Fusion + dark palette + qlementine-icons — small, dependency-light,
   works on Qt 6.4, transforms the first impression. Add the setup in
   `src/app/src/main.cpp` behind a small `applyTheme()` helper so light/dark
   can become a setting later; wire qlementine-icons into the CMake build.
2. **Later:** move to the full qlementine style when ready to bump Qt (or after
   confirming an older tag builds on 6.4) — it's the only option that delivers
   actual modern widget rendering with animations rather than a recolor.

## Sources

- <https://github.com/oclero/qlementine>
- <https://github.com/oclero/qlementine/releases>
- <https://github.com/oclero/qlementine-icons>
- <https://github.com/Alexhuszagh/BreezeStyleSheets>
- <https://github.com/ColinDuquesnoy/QDarkStyleSheet>
- <https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System>

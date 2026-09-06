#pragma once

// Viewport navigation schemes: named mouse-binding tables emulating the
// muscle memory of well-known CAD/DCC packages. A scheme maps
// (button, exact modifier set) -> camera drag mode; everything else about
// navigation (wheel zoom at cursor, left-click picking) is invariant.

#include "cadly/ui/CameraController.h"

#include <QString>
#include <Qt>

#include <array>

namespace cadly::ui {

enum class NavigationScheme {
  Cadly = 0,   // right orbits, middle pans (three-button-mouse layout)
  Blender,     // middle orbits, Shift+middle pans, Ctrl+middle zooms
  Rhino,       // right orbits, Shift+right pans, Ctrl+right zooms
  Fusion360,   // middle pans, Shift+middle orbits
  Maya,        // Alt+left orbits, Alt+middle pans, Alt+right zooms
};

inline constexpr std::array<NavigationScheme, 5> kAllNavigationSchemes{
  NavigationScheme::Cadly,     NavigationScheme::Blender,
  NavigationScheme::Rhino,     NavigationScheme::Fusion360,
  NavigationScheme::Maya,
};

// Exact-match lookup: `mods` must equal a binding's modifier set, so e.g.
// Alt+Ctrl+left never falls back onto the Alt+left row. Returns
// DragMode::None when the scheme has no row for the combination — a plain
// (unmodified) left button never resolves in any scheme; it stays reserved
// for picking.
//
// Every scheme also answers the two cross-scheme trackpad rows — Alt+left
// orbits, Alt+Ctrl+left pans (⌥ / ⌥⌘ on macOS) — unless the scheme itself
// claims that combination (Maya's Alt+left). Trackpads have no middle
// button, and half the schemes are unreachable without this.
CameraController::DragMode
resolve_navigation(NavigationScheme scheme, Qt::MouseButton button,
                   Qt::KeyboardModifiers mods);

// Stable settings token ("cadly", "blender", ...) and its inverse (unknown
// tokens fall back to Cadly).
QString           navigation_scheme_key(NavigationScheme scheme);
NavigationScheme  navigation_scheme_from_key(const QString& key);

// Orbit style (CameraController::OrbitStyle): settings token
// ("free"/"turntable"), inverse, and translated display name. Persisted as
// display/orbit_style; external consumers (the Quick Look extension) read
// the same token.
QString orbit_style_key(CameraController::OrbitStyle style);
CameraController::OrbitStyle orbit_style_from_key(const QString& key);
QString orbit_style_name(CameraController::OrbitStyle style);

// Display name for menus/combos. Product names stay untranslated.
QString navigation_scheme_name(NavigationScheme scheme);

// Multi-line, translated legend ("Orbit: right-drag · ⌥ left-drag" ...)
// rendered from the scheme's actual binding table with native modifier
// glyphs, plus the invariant zoom line.
QString navigation_scheme_legend(NavigationScheme scheme);

} // namespace cadly::ui

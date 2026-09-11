#pragma once

#include "cadly/input/Navigation.h"

#include <QString>

namespace cadly::input_qt {

// Presentation only: all bindings and persisted tokens live in Cadly::Input.
// Legends render the same registry the router resolves, with native modifier
// glyphs and the existing NavigationScheme translation context.
QString navigation_scheme_name(input::NavigationScheme scheme);
QString navigation_scheme_legend(input::NavigationScheme scheme);
QString orbit_style_name(input::OrbitStyle style);

} // namespace cadly::input_qt

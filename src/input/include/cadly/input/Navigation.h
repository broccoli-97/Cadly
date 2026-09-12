#pragma once

#include "cadly/input/NavigationCommand.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace cadly::input {

enum class Button : std::uint8_t {
  None = 0, Left = 1, Middle = 2, Right = 4, Other = 8,
};

constexpr Button operator|(Button a, Button b) {
  return static_cast<Button>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}

constexpr bool contains(Button buttons, Button button) {
  return (static_cast<unsigned>(buttons) & static_cast<unsigned>(button)) != 0;
}

// Logical shortcut modifiers, not physical key names. Primary is Command on
// macOS and Control elsewhere; Secondary is physical Control on macOS and
// Meta elsewhere. Adapters must retain unsupported modifiers as Other so an
// extra modifier never accidentally matches a shorter binding.
enum class Modifiers : std::uint8_t {
  None = 0, Shift = 1, Primary = 2, Alt = 4, Secondary = 8, Other = 16,
};

constexpr Modifiers operator|(Modifiers a, Modifiers b) {
  return static_cast<Modifiers>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}

constexpr bool contains(Modifiers modifiers, Modifiers modifier) {
  return (static_cast<unsigned>(modifiers) & static_cast<unsigned>(modifier)) != 0;
}

enum class NavigationAction { None, Orbit, Pan, Dolly };
enum class NavigationScheme { Cadly = 0, Blender, Rhino, Fusion360, Maya };
enum class OrbitStyle { Free = 0, Turntable };

struct Binding {
  Button button;
  Modifiers modifiers;
  NavigationAction action;
};

struct NavigationPreset {
  NavigationScheme scheme;
  std::string_view key;   // stable persisted token
  std::string_view name;  // product name; the host translates the default label
  std::vector<Binding> bindings;
};

// The registry is shared by resolution, serialization, and UI enumeration /
// legends. Common trackpad bindings are added once, without duplicate chords.
const std::vector<NavigationPreset>& navigation_presets();
const NavigationPreset& navigation_preset(NavigationScheme scheme);
NavigationAction resolve_navigation(NavigationScheme scheme, Button button,
                                    Modifiers modifiers);
std::string_view navigation_scheme_key(NavigationScheme scheme);
NavigationScheme navigation_scheme_from_key(std::string_view key);
std::string_view orbit_style_key(OrbitStyle style);
OrbitStyle orbit_style_from_key(std::string_view key);

struct Preferences {
  NavigationScheme navigation_scheme{NavigationScheme::Cadly};
  OrbitStyle orbit_style{OrbitStyle::Free};
};

bool operator==(const Preferences& a, const Preferences& b);
bool operator!=(const Preferences& a, const Preferences& b);
Preferences normalized(Preferences preferences);

struct ScrollEvent {
  Position position{};
  int angle_delta{0};
};

NavigationCommand drag_motion(NavigationAction action, Position from,
                              Position to, const Preferences& preferences);
// Wheel and drag conversion share the configuration boundary even though the
// current preferences only change bindings and orbit behavior.
NavigationCommand scroll_zoom(Position cursor, int angle_delta,
                               const Preferences& preferences = {});

} // namespace cadly::input

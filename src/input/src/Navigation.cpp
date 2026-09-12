#include "cadly/input/Navigation.h"

#include <algorithm>
#include <cmath>

namespace cadly::input {

const std::vector<NavigationPreset>& navigation_presets() {
  static const auto presets = [] {
    using B = Button;
    using M = Modifiers;
    using A = NavigationAction;
    std::vector<NavigationPreset> result{
      {NavigationScheme::Cadly, "cadly", "Cadly",
       {{B::Right, M::None, A::Orbit}, {B::Middle, M::None, A::Pan}}},
      {NavigationScheme::Blender, "blender", "Blender",
       {{B::Middle, M::None, A::Orbit}, {B::Middle, M::Shift, A::Pan},
        {B::Middle, M::Primary, A::Dolly}}},
      {NavigationScheme::Rhino, "rhino", "Rhino",
       {{B::Right, M::None, A::Orbit}, {B::Right, M::Shift, A::Pan},
        {B::Right, M::Primary, A::Dolly}}},
      {NavigationScheme::Fusion360, "fusion360", "Fusion 360",
       {{B::Middle, M::None, A::Pan}, {B::Middle, M::Shift, A::Orbit}}},
      {NavigationScheme::Maya, "maya", "Maya",
       {{B::Left, M::Alt, A::Orbit}, {B::Middle, M::Alt, A::Pan},
        {B::Right, M::Alt, A::Dolly}}},
    };
    const Binding trackpad[] = {
      {B::Left, M::Alt, A::Orbit},
      {B::Left, M::Alt | M::Primary, A::Pan},
    };
    for (auto& preset : result) {
      for (const auto& binding : trackpad) {
        const auto taken = std::find_if(preset.bindings.begin(), preset.bindings.end(),
          [&binding](const Binding& row) {
            return row.button == binding.button && row.modifiers == binding.modifiers;
          });
        if (taken == preset.bindings.end()) preset.bindings.push_back(binding);
      }
    }
    return result;
  }();
  return presets;
}

const NavigationPreset& navigation_preset(NavigationScheme scheme) {
  for (const auto& preset : navigation_presets()) {
    if (preset.scheme == scheme) return preset;
  }
  return navigation_presets().front();
}

NavigationAction resolve_navigation(NavigationScheme scheme, Button button,
                                    Modifiers modifiers) {
  for (const auto& row : navigation_preset(scheme).bindings) {
    if (row.button == button && row.modifiers == modifiers) return row.action;
  }
  return NavigationAction::None;
}

std::string_view navigation_scheme_key(NavigationScheme scheme) {
  return navigation_preset(scheme).key;
}

NavigationScheme navigation_scheme_from_key(std::string_view key) {
  for (const auto& preset : navigation_presets()) {
    if (preset.key == key) return preset.scheme;
  }
  return NavigationScheme::Cadly;
}

std::string_view orbit_style_key(OrbitStyle style) {
  return style == OrbitStyle::Turntable ? "turntable" : "free";
}

OrbitStyle orbit_style_from_key(std::string_view key) {
  return key == "turntable" ? OrbitStyle::Turntable : OrbitStyle::Free;
}

bool operator==(const Preferences& a, const Preferences& b) {
  return a.navigation_scheme == b.navigation_scheme && a.orbit_style == b.orbit_style;
}

bool operator!=(const Preferences& a, const Preferences& b) {
  return !(a == b);
}

Preferences normalized(Preferences preferences) {
  preferences.navigation_scheme = navigation_preset(preferences.navigation_scheme).scheme;
  preferences.orbit_style = orbit_style_from_key(orbit_style_key(preferences.orbit_style));
  return preferences;
}

NavigationCommand drag_motion(NavigationAction action, Position from,
                              Position to, const Preferences& preferences) {
  const float dx = static_cast<float>(to.x) - static_cast<float>(from.x);
  const float dy = static_cast<float>(to.y) - static_cast<float>(from.y);
  NavigationCommand command;
  command.position = to;
  if (dx == 0.0f && dy == 0.0f) return command;
  switch (action) {
    case NavigationAction::Orbit:
      command.type = preferences.orbit_style == OrbitStyle::Turntable
        ? CommandType::OrbitTurntable : CommandType::OrbitFree;
      command.x = -dx * 0.006f;
      command.y = -dy * 0.006f;
      break;
    case NavigationAction::Pan:
      command.type = CommandType::Pan;
      command.x = -dx * 0.0025f;
      command.y = dy * 0.0025f;
      break;
    case NavigationAction::Dolly:
      command.type = CommandType::Zoom;
      command.factor = std::max(0.0f, 1.0f + dy * 0.0025f);
      break;
    case NavigationAction::None:
      break;
  }
  return command;
}

NavigationCommand scroll_zoom(Position cursor, int angle_delta, const Preferences&) {
  NavigationCommand command;
  command.position = cursor;
  if (angle_delta != 0) {
    command.type = CommandType::ZoomAtCursor;
    command.factor = std::exp(-static_cast<float>(angle_delta) * 0.0015f);
  }
  return command;
}

} // namespace cadly::input

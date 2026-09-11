#include "cadly/input/Navigation.h"
#include "TestChecks.h"

#include <cmath>
#include <set>
#include <utility>

using namespace cadly::input;

namespace {
using B = Button;
using M = Modifiers;
using A = NavigationAction;
using S = NavigationScheme;

// Independent compatibility fixture for fa354d7. Do not derive the expected
// bindings from the production registry: a changed/missing row must fail.
const std::vector<NavigationPreset> legacy{
  {S::Cadly, "cadly", "Cadly",
   {{B::Right, M::None, A::Orbit}, {B::Middle, M::None, A::Pan}}},
  {S::Blender, "blender", "Blender",
   {{B::Middle, M::None, A::Orbit}, {B::Middle, M::Shift, A::Pan},
    {B::Middle, M::Primary, A::Dolly}}},
  {S::Rhino, "rhino", "Rhino",
   {{B::Right, M::None, A::Orbit}, {B::Right, M::Shift, A::Pan},
    {B::Right, M::Primary, A::Dolly}}},
  {S::Fusion360, "fusion360", "Fusion 360",
   {{B::Middle, M::None, A::Pan}, {B::Middle, M::Shift, A::Orbit}}},
  {S::Maya, "maya", "Maya",
   {{B::Left, M::Alt, A::Orbit}, {B::Middle, M::Alt, A::Pan},
    {B::Right, M::Alt, A::Dolly}}},
};

void binding_matrix() {
  CHECK(navigation_presets().size() == legacy.size());
  std::set<std::string_view> keys;
  for (const auto& expected : legacy) {
    const auto& actual = navigation_preset(expected.scheme);
    CHECK(actual.scheme == expected.scheme);
    CHECK(actual.key == expected.key);
    CHECK(actual.name == expected.name);
    CHECK(keys.insert(actual.key).second);
    CHECK(navigation_scheme_from_key(expected.key) == expected.scheme);
    CHECK(navigation_scheme_key(expected.scheme) == expected.key);
    std::set<std::pair<Button, Modifiers>> chords;
    for (const auto& row : actual.bindings) {
      CHECK(chords.emplace(row.button, row.modifiers).second);
      CHECK(row.action != A::None);
    }
    CHECK(actual.bindings.size() == expected.bindings.size() +
          (expected.scheme == S::Maya ? 1u : 2u));

    for (const auto button : {B::None, B::Left, B::Middle, B::Right, B::Other,
                              B::Left | B::Right, B::Middle | B::Other}) {
      for (unsigned bits = 0; bits < 32; ++bits) {
        const auto modifiers = static_cast<M>(bits);
        auto action = A::None;
        if (button == B::Left && modifiers == M::Alt) action = A::Orbit;
        if (button == B::Left && modifiers == (M::Alt | M::Primary)) action = A::Pan;
        for (const auto& row : expected.bindings) {
          if (row.button == button && row.modifiers == modifiers) action = row.action;
        }
        CHECK(resolve_navigation(expected.scheme, button, modifiers) == action);
      }
    }
  }
}

void defaults_and_tokens() {
  CHECK(Preferences{} == Preferences{S::Cadly, OrbitStyle::Free});
  for (const auto unknown : {"", "CADLY", "fusion", "Future Scheme", " cadly "}) {
    CHECK(navigation_scheme_from_key(unknown) == S::Cadly);
  }
  CHECK(orbit_style_key(OrbitStyle::Free) == "free");
  CHECK(orbit_style_key(OrbitStyle::Turntable) == "turntable");
  CHECK(orbit_style_from_key("free") == OrbitStyle::Free);
  CHECK(orbit_style_from_key("turntable") == OrbitStyle::Turntable);
  CHECK(orbit_style_from_key("TURNTABLE") == OrbitStyle::Free);
  CHECK(orbit_style_from_key("future") == OrbitStyle::Free);
  CHECK(orbit_style_from_key("") == OrbitStyle::Free);
  CHECK(normalized({static_cast<S>(99), static_cast<OrbitStyle>(99)}) == Preferences{});
  CHECK(Preferences{} != Preferences{S::Blender, OrbitStyle::Free});
  CHECK(Preferences{} != Preferences{S::Cadly, OrbitStyle::Turntable});
}

void motion_contract() {
  const Position from{100, 150};
  const Position to{120, 140};
  const auto orbit = drag_motion(A::Orbit, from, to, {});
  CHECK(orbit.type == CommandType::OrbitFree);
  CHECK(std::abs(orbit.x + 0.12f) < 1e-6f);
  CHECK(std::abs(orbit.y - 0.06f) < 1e-6f);
  const auto turntable = drag_motion(A::Orbit, from, to, {S::Maya, OrbitStyle::Turntable});
  CHECK(turntable.type == CommandType::OrbitTurntable);
  CHECK(turntable.x == orbit.x && turntable.y == orbit.y);
  const auto pan = drag_motion(A::Pan, from, to, {});
  CHECK(pan.type == CommandType::Pan);
  CHECK(std::abs(pan.x + 0.05f) < 1e-6f);
  CHECK(std::abs(pan.y + 0.025f) < 1e-6f);
  const auto dolly = drag_motion(A::Dolly, from, to, {});
  CHECK(dolly.type == CommandType::Zoom);
  CHECK(std::abs(dolly.factor - 0.975f) < 1e-6f);
  CHECK(drag_motion(A::Dolly, from, {100, -1000}, {}).factor == 0.0f);
  CHECK(drag_motion(A::None, from, to, {}).type == CommandType::None);
  for (const auto action : {A::Orbit, A::Pan, A::Dolly}) {
    CHECK(drag_motion(action, from, from, {}).type == CommandType::None);
  }
  for (const int delta : {1, 15, 120, 240, -1, -15, -120, -240}) {
    const auto scroll = scroll_zoom({23, 45}, delta);
    CHECK(scroll.type == CommandType::ZoomAtCursor);
    CHECK(scroll.position.x == 23 && scroll.position.y == 45);
    CHECK(std::abs(scroll.factor - std::exp(-static_cast<float>(delta) * 0.0015f)) < 1e-6f);
    CHECK(std::abs(scroll.factor * scroll_zoom({}, -delta).factor - 1.0f) < 1e-6f);
  }
  CHECK(scroll_zoom({}, 0).type == CommandType::None);
}
} // namespace

int main() {
  binding_matrix();
  defaults_and_tokens();
  motion_contract();
  return cadly::tests::report();
}

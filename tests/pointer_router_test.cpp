#include "cadly/input/PointerRouter.h"
#include "TestChecks.h"

#include <cmath>

using namespace cadly::input;

namespace {
using B = Button;
using M = Modifiers;
using Target = PointerTarget;
using Phase = PointerPhase;
using Type = CommandType;

PointerEvent pointer(B changed, B held, M modifiers = M::None, Position pos = {100, 100}) {
  return {pos, changed, held, modifiers};
}

void every_binding_has_a_complete_gesture() {
  for (const auto& preset : navigation_presets()) {
    for (const auto style : {OrbitStyle::Free, OrbitStyle::Turntable}) {
      for (const auto& row : preset.bindings) {
        PointerRouter router;
        router.set_preferences({preset.scheme, style});
        const auto start = router.press(pointer(row.button, row.button, row.modifiers), true);
        CHECK(start.consumed && start.target == Target::Navigation && start.phase == Phase::Begin);
        CHECK(router.navigating() && !router.tool_active());
        CHECK(start.navigation.type == (row.action == NavigationAction::Orbit ? Type::BeginOrbit : Type::None));
        // Modifier changes cannot change an owned gesture into another action.
        const auto move = router.move(pointer(B::None, row.button, M::Other, {110, 120}));
        CHECK(move.consumed && move.phase == Phase::Update && move.target == Target::Navigation);
        const auto expected = drag_motion(row.action, {100, 100}, {110, 120}, {preset.scheme, style});
        CHECK(move.navigation.type == expected.type);
        CHECK(move.navigation.x == expected.x && move.navigation.y == expected.y);
        CHECK(move.navigation.factor == expected.factor);
        const auto end = router.release(pointer(row.button, B::None));
        CHECK(end.consumed && end.target == Target::Navigation && end.phase == Phase::End);
        CHECK(end.navigation.type == (row.action == NavigationAction::Orbit ? Type::EndOrbit : Type::None));
        CHECK(!router.navigating() && !router.tool_active());
        CHECK(!router.move(pointer(B::None, B::None)).consumed);
      }
    }
  }
}

void tool_and_selection_priority() {
  for (const auto& preset : navigation_presets()) {
    PointerRouter router;
    router.set_preferences({preset.scheme, OrbitStyle::Free});
    const auto pick = router.press(pointer(B::Left, B::Left), false);
    CHECK(pick.target == Target::Selection && pick.consumed);
    CHECK(!router.move(pointer(B::None, B::Left)).consumed);
    router.release(pointer(B::Left, B::None));
    const auto tool = router.press(pointer(B::Left, B::Left, M::Shift), true);
    CHECK(tool.target == Target::Tool && tool.phase == Phase::Begin && tool.consumed);
    CHECK(router.tool_active());
    for (const auto modifiers : {M::None, M::Alt, M::Alt | M::Primary, M::Other}) {
      const auto move = router.move(pointer(B::None, B::Left, modifiers));
      CHECK(move.target == Target::Tool && move.phase == Phase::Update);
      CHECK(move.navigation.type == Type::None && !router.navigating());
    }
    const auto end = router.release(pointer(B::Left, B::None));
    CHECK(end.target == Target::Tool && end.phase == Phase::End && !router.tool_active());
    CHECK(router.press(pointer(B::Left, B::Left, M::Alt), true).target == Target::Navigation);
  }
}

void late_modifiers_do_not_jump() {
  PointerRouter router;
  router.press(pointer(B::Left, B::Left), false);
  CHECK(!router.move(pointer(B::None, B::Left, M::None, {500, 500})).consumed);
  const auto promoted = router.move(pointer(B::None, B::Left, M::Alt, {700, 700}));
  CHECK(promoted.phase == Phase::Begin && promoted.navigation.type == Type::BeginOrbit);
  CHECK(promoted.navigation.position.x == 700 && promoted.navigation.position.y == 700);
  const auto next = router.move(pointer(B::None, B::Left, M::None, {710, 720}));
  CHECK(std::abs(next.navigation.x + 0.06f) < 1e-6f);
  CHECK(std::abs(next.navigation.y + 0.12f) < 1e-6f);
  router.release(pointer(B::Left, B::None));
  // Native/synthesized streams can omit the press entirely.
  const auto pan = router.move(pointer(B::None, B::Left, M::Alt | M::Primary));
  CHECK(pan.target == Target::Navigation && pan.phase == Phase::Begin);
  CHECK(pan.navigation.type == Type::None);
  CHECK(router.move(pointer(B::None, B::Left, M::None, {110, 110})).navigation.type == Type::Pan);
}

void settings_are_snapshotted_per_gesture() {
  PointerRouter router;
  router.press(pointer(B::Right, B::Right), false);
  router.set_preferences({NavigationScheme::Maya, OrbitStyle::Turntable});
  CHECK(router.move(pointer(B::None, B::Right, M::Alt, {110, 110})).navigation.type == Type::OrbitFree);
  router.release(pointer(B::Right, B::None));
  CHECK(!router.press(pointer(B::Right, B::Right), false).consumed);
  const auto late = router.move(pointer(B::None, B::Right, M::Alt));
  CHECK(late.target == Target::Navigation && late.phase == Phase::Begin);
  CHECK(router.move(pointer(B::None, B::Right, M::Alt, {110, 110})).navigation.type == Type::Zoom);
  router.release(pointer(B::Right, B::None));
  router.press(pointer(B::Left, B::Left, M::Alt), false);
  CHECK(router.move(pointer(B::None, B::Left, M::Alt, {110, 110})).navigation.type == Type::OrbitTurntable);
}

void extra_buttons_cannot_steal_or_end_a_drag() {
  PointerRouter router;
  router.press(pointer(B::Right, B::Right), false);
  const auto extra = router.press(pointer(B::Left, B::Left | B::Right), true);
  CHECK(extra.consumed && extra.target == Target::None);
  CHECK(router.navigating() && !router.tool_active());
  CHECK(router.release(pointer(B::Left, B::Right)).navigation.type == Type::None);
  CHECK(router.navigating());
  CHECK(router.move(pointer(B::None, B::Right, M::None, {110, 110})).navigation.type == Type::OrbitFree);
  CHECK(router.release(pointer(B::Right, B::Middle)).navigation.type == Type::EndOrbit);
  CHECK(!router.move(pointer(B::None, B::Middle)).consumed);
  router.release(pointer(B::Middle, B::None));
  CHECK(router.press(pointer(B::Middle, B::Middle), false).target == Target::Navigation);

  PointerRouter tool;
  tool.press(pointer(B::Left, B::Left), true);
  CHECK(tool.press(pointer(B::Right, B::Left | B::Right), false).target == Target::None);
  CHECK(tool.release(pointer(B::Right, B::Left)).target == Target::None);
  CHECK(tool.tool_active());
  CHECK(tool.move(pointer(B::None, B::Left, M::Alt)).target == Target::Tool);
}

void missing_releases_and_cancellation() {
  for (const bool is_tool : {false, true}) {
    const auto button = is_tool ? B::Left : B::Right;
    const auto target = is_tool ? Target::Tool : Target::Navigation;
    PointerRouter router;
    router.press(pointer(button, button), is_tool);
    const auto lost = router.move(pointer(B::None, B::None));
    CHECK(lost.target == target && lost.phase == Phase::End);
    CHECK(!router.navigating() && !router.tool_active());
    router.press(pointer(button, button), is_tool);
    const auto cancelled = router.cancel();
    CHECK(cancelled.target == target && cancelled.phase == Phase::End);
    CHECK(!router.cancel().consumed);
    CHECK(!router.move(pointer(B::None, button, M::Alt)).consumed);
    CHECK(!router.move(pointer(B::None, B::Right)).consumed);
    // Explicit fresh press works even if focus transfer lost the old release.
    CHECK(router.press(pointer(B::Right, B::Right), false).target == Target::Navigation);
    router.cancel();
    router.move(pointer(B::None, B::None));
    CHECK(router.move(pointer(B::None, B::Right)).phase == Phase::Begin);
  }
}

void unbound_chords_and_independent_viewports() {
  PointerRouter first, second;
  second.set_preferences({NavigationScheme::Blender, OrbitStyle::Turntable});
  for (const auto buttons : {B::None, B::Other, B::Left | B::Right, B::Right | B::Other}) {
    CHECK(!first.press(pointer(B::Right, buttons), true).consumed);
    CHECK(!first.move(pointer(B::None, buttons, M::Alt)).consumed);
  }
  CHECK(first.press(pointer(B::Middle, B::Middle), false).target == Target::Navigation);
  CHECK(!second.navigating());
  second.press(pointer(B::Middle, B::Middle), false);
  CHECK(first.move(pointer(B::None, B::Middle, M::None, {110, 110})).navigation.type == Type::Pan);
  CHECK(second.move(pointer(B::None, B::Middle, M::None, {110, 110})).navigation.type == Type::OrbitTurntable);
  first.cancel();
  CHECK(second.navigating());
}

void wheel_is_configured_but_independent_of_pointer_capture() {
  for (const auto& preset : navigation_presets()) {
    PointerRouter router;
    const Preferences preferences{preset.scheme, OrbitStyle::Turntable};
    router.set_preferences(preferences);
    const auto expected = scroll_zoom({30, 60}, 120, preferences);
    router.press(pointer(B::Left, B::Left), true);
    const auto captured = router.scroll({{30, 60}, 120});
    CHECK(captured.type == Type::ZoomAtCursor && captured.factor == expected.factor);
    CHECK(captured.position.x == 30 && captured.position.y == 60);
    CHECK(router.tool_active());
    router.cancel();
    CHECK(router.scroll({{30, 60}, 120}).factor == expected.factor);
    CHECK(router.scroll({{}, 0}).type == Type::None);
  }
}
} // namespace

int main() {
  every_binding_has_a_complete_gesture();
  tool_and_selection_priority();
  late_modifiers_do_not_jump();
  settings_are_snapshotted_per_gesture();
  extra_buttons_cannot_steal_or_end_a_drag();
  missing_releases_and_cancellation();
  unbound_chords_and_independent_viewports();
  wheel_is_configured_but_independent_of_pointer_capture();
  return cadly::tests::report();
}

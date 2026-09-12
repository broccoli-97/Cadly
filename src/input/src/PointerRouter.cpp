#include "cadly/input/PointerRouter.h"

namespace cadly::input {

namespace {
bool single_button(Button buttons) {
  return buttons == Button::Left || buttons == Button::Middle || buttons == Button::Right;
}
} // namespace

void PointerRouter::set_preferences(Preferences preferences) {
  preferences_ = normalized(preferences);
}

PointerResult PointerRouter::begin_navigation(const PointerEvent& event,
                                             Button button,
                                             NavigationAction action) {
  owner_ = PointerTarget::Navigation;
  button_ = button;
  action_ = action;
  drag_preferences_ = preferences_;
  last_position_ = event.position;
  PointerResult result{owner_, PointerPhase::Begin, {}, true};
  if (action == NavigationAction::Orbit) {
    result.navigation.type = CommandType::BeginOrbit;
    result.navigation.position = event.position;
  }
  return result;
}

PointerResult PointerRouter::press(const PointerEvent& event, bool tool_hit) {
  if (owner_ != PointerTarget::None) {
    return {PointerTarget::None, PointerPhase::Update, {}, true};
  }
  if (!single_button(event.button) || event.buttons != event.button) return {};
  suppressed_ = false;
  const auto action = resolve_navigation(preferences_.navigation_scheme,
                                         event.button, event.modifiers);
  if (action != NavigationAction::None) {
    return begin_navigation(event, event.button, action);
  }
  if (event.button == Button::Left) {
    if (tool_hit) {
      owner_ = PointerTarget::Tool;
      button_ = event.button;
      return {owner_, PointerPhase::Begin, {}, true};
    }
    return {PointerTarget::Selection, PointerPhase::Begin, {}, true};
  }
  return {};
}

PointerResult PointerRouter::move(const PointerEvent& event) {
  if (owner_ != PointerTarget::None) {
    // Lost release (focus transfer, synthesized events): finish the owner
    // before considering any new gesture, and do not move it one last time.
    if (!contains(event.buttons, button_)) return finish(event.buttons);
    PointerResult result{owner_, PointerPhase::Update, {}, true};
    if (navigating()) {
      result.navigation = drag_motion(action_, last_position_, event.position,
                                       drag_preferences_);
      last_position_ = event.position;
    }
    return result;
  }
  if (event.buttons == Button::None) suppressed_ = false;
  if (suppressed_ || !single_button(event.buttons)) return {};
  const auto action = resolve_navigation(preferences_.navigation_scheme,
                                         event.buttons, event.modifiers);
  if (action != NavigationAction::None) {
    // No jump: the first move with the late modifier becomes the anchor.
    return begin_navigation(event, event.buttons, action);
  }
  return {};
}

PointerResult PointerRouter::finish(Button remaining_buttons) {
  PointerResult result{owner_, PointerPhase::End, {}, owner_ != PointerTarget::None};
  if (navigating() && action_ == NavigationAction::Orbit) {
    result.navigation.type = CommandType::EndOrbit;
  }
  owner_ = PointerTarget::None;
  button_ = Button::None;
  action_ = NavigationAction::None;
  suppressed_ = remaining_buttons != Button::None;
  return result;
}

PointerResult PointerRouter::release(const PointerEvent& event) {
  if (owner_ != PointerTarget::None) {
    if (event.button == button_ || !contains(event.buttons, button_)) {
      return finish(event.buttons);
    }
    return {PointerTarget::None, PointerPhase::Update, {}, true};
  }
  if (event.buttons == Button::None) suppressed_ = false;
  return {};
}

PointerResult PointerRouter::cancel() {
  const auto result = finish(Button::None);
  suppressed_ = true;
  return result;
}

NavigationCommand PointerRouter::scroll(const ScrollEvent& event) const {
  return scroll_zoom(event.position, event.angle_delta, preferences_);
}

} // namespace cadly::input

#pragma once

#include "cadly/input/Navigation.h"

namespace cadly::input {

struct PointerEvent {
  Position position{};
  Button button{Button::None};   // changed button (press/release only)
  Button buttons{Button::None};  // currently held buttons
  Modifiers modifiers{Modifiers::None};
};

enum class PointerTarget { None, Navigation, Tool, Selection };
enum class PointerPhase { Begin, Update, End };

struct PointerResult {
  PointerTarget target{PointerTarget::None};
  PointerPhase phase{PointerPhase::Update};
  NavigationCommand navigation;
  bool consumed{false};
};

// Gesture ownership is independent of hit testing and camera math. The host
// supplies a tool hit on press, then routes Tool results to that tool until
// End. Priority is navigation binding > tool hit > selection. Late modifier
// promotion only applies to unclaimed gestures, including synthesized moves
// whose press was not delivered. A captured gesture keeps its original button,
// action, and preference snapshot until release/cancel.
class PointerRouter {
public:
  void set_preferences(Preferences preferences);
  const Preferences& preferences() const { return preferences_; }

  PointerResult press(const PointerEvent& event, bool tool_hit);
  PointerResult move(const PointerEvent& event);
  PointerResult release(const PointerEvent& event);
  PointerResult cancel();
  NavigationCommand scroll(const ScrollEvent& event) const;

  bool navigating() const { return owner_ == PointerTarget::Navigation; }
  bool tool_active() const { return owner_ == PointerTarget::Tool; }

private:
  PointerResult begin_navigation(const PointerEvent& event, Button button,
                                 NavigationAction action);
  PointerResult finish(Button remaining_buttons);

  Preferences preferences_;
  Preferences drag_preferences_;
  PointerTarget owner_{PointerTarget::None};
  Button button_{Button::None};
  NavigationAction action_{NavigationAction::None};
  Position last_position_{};
  // A cancelled gesture must not restart via late promotion on the next move.
  // Clear on all-buttons-up or a fresh, single-button press.
  bool suppressed_{false};
};

} // namespace cadly::input

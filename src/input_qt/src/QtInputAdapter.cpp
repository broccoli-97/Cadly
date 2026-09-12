#include "cadly/input_qt/QtInputAdapter.h"

#include <QMouseEvent>
#include <QWheelEvent>

namespace cadly::input_qt {

input::Button button_from_qt(Qt::MouseButton button) {
  switch (button) {
    case Qt::NoButton:     return input::Button::None;
    case Qt::LeftButton:   return input::Button::Left;
    case Qt::MiddleButton: return input::Button::Middle;
    case Qt::RightButton:  return input::Button::Right;
    default:              return input::Button::Other;
  }
}

input::Button buttons_from_qt(Qt::MouseButtons buttons) {
  auto result = input::Button::None;
  for (const auto button : {Qt::LeftButton, Qt::MiddleButton, Qt::RightButton}) {
    if (buttons.testFlag(button)) result = result | button_from_qt(button);
  }
  const auto known = Qt::LeftButton | Qt::MiddleButton | Qt::RightButton;
  if ((buttons & ~known) != Qt::NoButton) result = result | input::Button::Other;
  return result;
}

input::Modifiers modifiers_from_qt(Qt::KeyboardModifiers modifiers) {
  using M = input::Modifiers;
  auto result = M::None;
  if (modifiers.testFlag(Qt::ShiftModifier)) result = result | M::Shift;
  if (modifiers.testFlag(Qt::AltModifier)) result = result | M::Alt;
  // Qt already maps macOS Command -> ControlModifier and physical Control ->
  // MetaModifier. Swapping again here would break existing trackpad bindings.
  if (modifiers.testFlag(Qt::ControlModifier)) result = result | M::Primary;
  if (modifiers.testFlag(Qt::MetaModifier)) result = result | M::Secondary;
  const auto known = Qt::ShiftModifier | Qt::AltModifier |
                     Qt::ControlModifier | Qt::MetaModifier;
  if ((modifiers & ~known) != Qt::NoModifier) result = result | M::Other;
  return result;
}

Qt::KeyboardModifiers modifiers_to_qt(input::Modifiers modifiers) {
  using M = input::Modifiers;
  Qt::KeyboardModifiers result;
  if (input::contains(modifiers, M::Shift)) result |= Qt::ShiftModifier;
  if (input::contains(modifiers, M::Alt)) result |= Qt::AltModifier;
  if (input::contains(modifiers, M::Primary)) result |= Qt::ControlModifier;
  if (input::contains(modifiers, M::Secondary)) result |= Qt::MetaModifier;
  return result;
}

input::PointerEvent pointer_event(const QMouseEvent& event) {
  const auto pos = event.position().toPoint();
  return {{pos.x(), pos.y()}, button_from_qt(event.button()),
          buttons_from_qt(event.buttons()), modifiers_from_qt(event.modifiers())};
}

input::ScrollEvent scroll_event(const QWheelEvent& event) {
  const auto pos = event.position().toPoint();
  // Preserve the angular-wheel contract (including small trackpad deltas).
  // Qt has already applied natural-scroll direction; inverted() is metadata,
  // not a request to negate again. Do not add pixelDelta to angleDelta.
  return {{pos.x(), pos.y()}, event.angleDelta().y()};
}

} // namespace cadly::input_qt

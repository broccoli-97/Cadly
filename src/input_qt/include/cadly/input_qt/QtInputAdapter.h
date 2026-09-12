#pragma once

#include "cadly/input/PointerRouter.h"

#include <Qt>

class QMouseEvent;
class QWheelEvent;

namespace cadly::input_qt {

input::Button button_from_qt(Qt::MouseButton button);
input::Button buttons_from_qt(Qt::MouseButtons buttons);
input::Modifiers modifiers_from_qt(Qt::KeyboardModifiers modifiers);
Qt::KeyboardModifiers modifiers_to_qt(input::Modifiers modifiers);
input::PointerEvent pointer_event(const QMouseEvent& event);
input::ScrollEvent scroll_event(const QWheelEvent& event);

} // namespace cadly::input_qt

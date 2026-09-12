#include "cadly/input_qt/InputPreferences.h"

namespace cadly::input_qt {

InputPreferences::InputPreferences(input::Preferences initial, QObject* parent)
  : QObject(parent), value_(input::normalized(initial)) {}

void InputPreferences::set_value(input::Preferences value) {
  value = input::normalized(value);
  if (value == value_) return;
  value_ = value;
  emit changed();
}

} // namespace cadly::input_qt

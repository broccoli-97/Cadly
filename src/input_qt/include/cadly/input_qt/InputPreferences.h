#pragma once

#include "cadly/input/Navigation.h"

#include <QObject>

namespace cadly::input_qt {

// Shared, observable runtime configuration. No disk I/O or widget/camera
// ownership: the app loads/persists it, editors modify a copy of value(), and
// any number of viewports subscribe once to the complete configuration.
class InputPreferences : public QObject {
  Q_OBJECT
public:
  explicit InputPreferences(input::Preferences initial = {}, QObject* parent = nullptr);

  const input::Preferences& value() const { return value_; }
  void set_value(input::Preferences value);

signals:
  void changed();

private:
  input::Preferences value_;
};

} // namespace cadly::input_qt

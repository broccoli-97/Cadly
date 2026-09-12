#include "cadly/input_qt/InputPreferences.h"
#include "cadly/input_qt/NavigationLabels.h"
#include "cadly/input_qt/QtInputAdapter.h"
#include "TestChecks.h"

#include <QGuiApplication>
#include <QMouseEvent>
#include <QWheelEvent>

#include <cmath>

using namespace cadly;

namespace {
void buttons_and_modifiers() {
  using B = input::Button;
  using M = input::Modifiers;
  CHECK(input_qt::button_from_qt(Qt::NoButton) == B::None);
  CHECK(input_qt::button_from_qt(Qt::LeftButton) == B::Left);
  CHECK(input_qt::button_from_qt(Qt::MiddleButton) == B::Middle);
  CHECK(input_qt::button_from_qt(Qt::RightButton) == B::Right);
  CHECK(input_qt::button_from_qt(Qt::BackButton) == B::Other);
  CHECK(input_qt::buttons_from_qt(Qt::LeftButton | Qt::RightButton) == (B::Left | B::Right));
  CHECK(input_qt::buttons_from_qt(Qt::MiddleButton | Qt::BackButton) == (B::Middle | B::Other));
  CHECK(input_qt::buttons_from_qt(Qt::NoButton) == B::None);

  const Qt::KeyboardModifier qt_modifiers[] = {
    Qt::ShiftModifier, Qt::ControlModifier, Qt::AltModifier,
    Qt::MetaModifier, Qt::KeypadModifier, Qt::GroupSwitchModifier,
  };
  const M modifiers[] = {M::Shift, M::Primary, M::Alt, M::Secondary, M::Other, M::Other};
  for (unsigned mask = 0; mask < 64; ++mask) {
    Qt::KeyboardModifiers native;
    auto expected = M::None;
    for (unsigned bit = 0; bit < 6; ++bit) {
      if ((mask & (1u << bit)) == 0) continue;
      native |= qt_modifiers[bit];
      expected = expected | modifiers[bit];
    }
    CHECK(input_qt::modifiers_from_qt(native) == expected);
    if (mask < 16) CHECK(input_qt::modifiers_to_qt(expected) == native);
  }

  // These are Qt's logical flags on every OS. In particular, macOS's physical
  // Control is MetaModifier, not ControlModifier (which is Command there).
  for (const auto& preset : input::navigation_presets()) {
    const auto resolve = [&](Qt::KeyboardModifiers mods) {
      return input::resolve_navigation(preset.scheme, B::Left,
                                        input_qt::modifiers_from_qt(mods));
    };
    CHECK(resolve(Qt::AltModifier) == input::NavigationAction::Orbit);
    CHECK(resolve(Qt::AltModifier | Qt::ControlModifier) == input::NavigationAction::Pan);
    CHECK(resolve(Qt::AltModifier | Qt::MetaModifier) == input::NavigationAction::None);
    CHECK(resolve(Qt::AltModifier | Qt::KeypadModifier) == input::NavigationAction::None);
    CHECK(resolve(Qt::AltModifier | Qt::GroupSwitchModifier) == input::NavigationAction::None);
  }
}

void event_coordinates_and_wheel_direction() {
  QMouseEvent mouse(QEvent::MouseMove, QPointF(25.4, 30.7), QPointF(800, 900),
                    Qt::NoButton, Qt::LeftButton | Qt::MiddleButton,
                    Qt::AltModifier | Qt::ControlModifier);
  const auto normalized = input_qt::pointer_event(mouse);
  CHECK(normalized.position.x == 25 && normalized.position.y == 31);
  CHECK(normalized.button == input::Button::None);
  CHECK(normalized.buttons == (input::Button::Left | input::Button::Middle));
  CHECK(normalized.modifiers == (input::Modifiers::Alt | input::Modifiers::Primary));

  for (const bool inverted : {false, true}) {
    for (const auto phase : {Qt::NoScrollPhase, Qt::ScrollBegin, Qt::ScrollUpdate, Qt::ScrollEnd}) {
      for (const int delta : {1, 15, 120, -1, -15, -120}) {
        QWheelEvent wheel(QPointF(40, 70), QPointF(900, 800), QPoint(13, 80),
                          QPoint(0, delta), Qt::NoButton, Qt::NoModifier, phase, inverted);
        const auto scroll = input_qt::scroll_event(wheel);
        CHECK(scroll.angle_delta == delta);
        const auto command = input::PointerRouter{}.scroll(scroll);
        CHECK(command.type == input::CommandType::ZoomAtCursor);
        CHECK(command.position.x == 40 && command.position.y == 70);
        CHECK(std::abs(command.factor - std::exp(-static_cast<float>(delta) * 0.0015f)) < 1e-6f);
      }
    }
  }
  // Retain the existing angular-wheel contract: no horizontal zoom or double
  // counting the pixel delta supplied alongside a trackpad angular delta.
  QWheelEvent horizontal({}, {}, QPoint(0, 90), QPoint(120, 0),
                         Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
  CHECK(input_qt::scroll_event(horizontal).angle_delta == 0);
}

void labels_and_native_glyphs() {
  for (const auto& preset : input::navigation_presets()) {
    CHECK(!input_qt::navigation_scheme_name(preset.scheme).isEmpty());
    const auto legend = input_qt::navigation_scheme_legend(preset.scheme);
    CHECK(legend.contains(QStringLiteral("Orbit:")));
    CHECK(legend.contains(QStringLiteral("Pan:")));
    CHECK(legend.contains(QStringLiteral("Zoom:")));
    CHECK(legend.contains(QStringLiteral("Left click stays reserved for selection.")));
    CHECK(legend.contains(QStringLiteral("left-drag")));
#ifdef Q_OS_MACOS
    CHECK(legend.contains(QString::fromUtf8("\xe2\x8c\xa5")));  // Option
    CHECK(legend.contains(QString::fromUtf8("\xe2\x8c\x98")));  // Command
#else
    CHECK(legend.contains(QStringLiteral("Alt")));
    CHECK(legend.contains(QStringLiteral("Ctrl")));
#endif
    const bool has_dolly = preset.scheme == input::NavigationScheme::Blender ||
                           preset.scheme == input::NavigationScheme::Rhino ||
                           preset.scheme == input::NavigationScheme::Maya;
    CHECK(legend.contains(QStringLiteral("Zoom: scroll (anchored")) == has_dolly);
  }
  CHECK(input_qt::orbit_style_name(input::OrbitStyle::Free) == QStringLiteral("Free orbit"));
  CHECK(input_qt::orbit_style_name(input::OrbitStyle::Turntable) == QStringLiteral("Turntable"));
}

void observable_preferences() {
  input_qt::InputPreferences preferences;
  input::PointerRouter first, second;
  int notifications = 0;
  QObject::connect(&preferences, &input_qt::InputPreferences::changed, [&]() {
    ++notifications;
    first.set_preferences(preferences.value());
    second.set_preferences(preferences.value());
  });
  preferences.set_value({});
  CHECK(notifications == 0);
  const input::Preferences value{input::NavigationScheme::Maya, input::OrbitStyle::Turntable};
  preferences.set_value(value);
  CHECK(notifications == 1 && preferences.value() == value);
  CHECK(first.preferences() == value && second.preferences() == value);
  preferences.set_value(value);
  CHECK(notifications == 1);
  preferences.set_value({static_cast<input::NavigationScheme>(99), static_cast<input::OrbitStyle>(99)});
  CHECK(notifications == 2 && preferences.value() == input::Preferences{});
  CHECK(first.preferences() == second.preferences());
}
} // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  buttons_and_modifiers();
  event_coordinates_and_wheel_direction();
  labels_and_native_glyphs();
  observable_preferences();
  return tests::report();
}

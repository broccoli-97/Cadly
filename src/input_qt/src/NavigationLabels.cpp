#include "cadly/input_qt/NavigationLabels.h"

#include "cadly/input_qt/QtInputAdapter.h"

#include <QCoreApplication>
#include <QKeySequence>
#include <QStringList>

namespace cadly::input_qt {

namespace {
QString describe(const input::Binding& binding) {
  QString mods;
  if (binding.modifiers != input::Modifiers::None) {
    mods = QKeySequence(static_cast<int>(modifiers_to_qt(binding.modifiers).toInt()))
             .toString(QKeySequence::NativeText);
    while (mods.endsWith(QLatin1Char('+'))) mods.chop(1);
    mods = mods.trimmed() + QLatin1Char(' ');
  }
  switch (binding.button) {
    case input::Button::Left:
      return mods + QCoreApplication::translate("NavigationScheme", "left-drag");
    case input::Button::Middle:
      return mods + QCoreApplication::translate("NavigationScheme", "middle-drag");
    case input::Button::Right:
      return mods + QCoreApplication::translate("NavigationScheme", "right-drag");
    default:
      return mods;
  }
}
} // namespace

QString navigation_scheme_name(input::NavigationScheme scheme) {
  const auto& preset = input::navigation_preset(scheme);
  if (preset.scheme == input::NavigationScheme::Cadly) {
    return QCoreApplication::translate("NavigationScheme", "Cadly (default)");
  }
  return QString::fromUtf8(preset.name.data(), static_cast<int>(preset.name.size()));
}

QString orbit_style_name(input::OrbitStyle style) {
  return style == input::OrbitStyle::Turntable
    ? QCoreApplication::translate("NavigationScheme", "Turntable")
    : QCoreApplication::translate("NavigationScheme", "Free orbit");
}

QString navigation_scheme_legend(input::NavigationScheme scheme) {
  const auto& rows = input::navigation_preset(scheme).bindings;
  const auto join = [&rows](input::NavigationAction action) {
    QStringList parts;
    for (const auto& row : rows) {
      if (row.action == action) parts << describe(row);
    }
    return parts.join(QCoreApplication::translate("NavigationScheme", " or "));
  };
  QStringList lines;
  lines << QCoreApplication::translate("NavigationScheme", "Orbit: %1")
             .arg(join(input::NavigationAction::Orbit));
  lines << QCoreApplication::translate("NavigationScheme", "Pan: %1")
             .arg(join(input::NavigationAction::Pan));
  const QString dolly = join(input::NavigationAction::Dolly);
  if (dolly.isEmpty()) {
    lines << QCoreApplication::translate(
      "NavigationScheme", "Zoom: scroll, anchored under the cursor");
  } else {
    lines << QCoreApplication::translate(
      "NavigationScheme", "Zoom: scroll (anchored under the cursor) or %1").arg(dolly);
  }
  lines << QCoreApplication::translate(
    "NavigationScheme", "Left click stays reserved for selection.");
  return lines.join(QLatin1Char('\n'));
}

} // namespace cadly::input_qt

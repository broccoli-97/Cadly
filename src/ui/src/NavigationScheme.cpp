#include "cadly/ui/NavigationScheme.h"

#include <QCoreApplication>
#include <QKeySequence>
#include <QStringList>

#include <vector>

namespace cadly::ui {

namespace {

// Free functions can't inherit tr(); each user-visible string is a literal
// QCoreApplication::translate("NavigationScheme", ...) call because lupdate
// only extracts direct calls (same convention as IsolateBanner) — a wrapper
// function would hide them from the catalog.

using DM = CameraController::DragMode;

struct Binding {
  Qt::MouseButton       button;
  Qt::KeyboardModifiers mods;
  DM                    mode;
};

// The scheme's own rows. Cross-scheme trackpad rows are appended in
// bindings_for() so a scheme table stays a faithful copy of the package it
// emulates.
std::vector<Binding> native_bindings(NavigationScheme s) {
  switch (s) {
    case NavigationScheme::Cadly:
      return {{Qt::RightButton,  Qt::NoModifier,       DM::Orbit},
              {Qt::MiddleButton, Qt::NoModifier,       DM::Pan}};
    case NavigationScheme::Blender:
      return {{Qt::MiddleButton, Qt::NoModifier,       DM::Orbit},
              {Qt::MiddleButton, Qt::ShiftModifier,    DM::Pan},
              {Qt::MiddleButton, Qt::ControlModifier,  DM::Dolly}};
    case NavigationScheme::Rhino:
      return {{Qt::RightButton,  Qt::NoModifier,       DM::Orbit},
              {Qt::RightButton,  Qt::ShiftModifier,    DM::Pan},
              {Qt::RightButton,  Qt::ControlModifier,  DM::Dolly}};
    case NavigationScheme::Fusion360:
      return {{Qt::MiddleButton, Qt::NoModifier,       DM::Pan},
              {Qt::MiddleButton, Qt::ShiftModifier,    DM::Orbit}};
    case NavigationScheme::Maya:
      return {{Qt::LeftButton,   Qt::AltModifier,      DM::Orbit},
              {Qt::MiddleButton, Qt::AltModifier,      DM::Pan},
              {Qt::RightButton,  Qt::AltModifier,      DM::Dolly}};
  }
  return {};
}

std::vector<Binding> bindings_for(NavigationScheme s) {
  auto rows = native_bindings(s);
  // Trackpad rows, added only where the scheme doesn't claim the combo.
  const Binding trackpad[] = {
    {Qt::LeftButton, Qt::AltModifier,                      DM::Orbit},
    {Qt::LeftButton, Qt::AltModifier | Qt::ControlModifier, DM::Pan},
  };
  for (const auto& t : trackpad) {
    bool taken = false;
    for (const auto& r : rows) {
      if (r.button == t.button && r.mods == t.mods) { taken = true; break; }
    }
    if (!taken) rows.push_back(t);
  }
  return rows;
}

// "⌥ left-drag" / "Ctrl+Shift middle-drag": native modifier glyphs via
// QKeySequence, then the button name.
QString describe(const Binding& b) {
  QString mods;
  if (b.mods != Qt::NoModifier) {
    // toString(NativeText) on a modifier-only sequence yields "⌥" on macOS
    // and "Alt+" on the others; normalise the trailing '+' away.
    mods = QKeySequence(static_cast<int>(b.mods.toInt())).toString(
      QKeySequence::NativeText);
    while (mods.endsWith(QLatin1Char('+'))) mods.chop(1);
    mods += QLatin1Char(' ');
    mods = mods.trimmed() + QLatin1Char(' ');
  }
  switch (b.button) {
    case Qt::LeftButton:
      return mods + QCoreApplication::translate("NavigationScheme",
                                                "left-drag");
    case Qt::MiddleButton:
      return mods + QCoreApplication::translate("NavigationScheme",
                                                "middle-drag");
    case Qt::RightButton:
      return mods + QCoreApplication::translate("NavigationScheme",
                                                "right-drag");
    default:
      return mods;
  }
}

} // namespace

CameraController::DragMode
resolve_navigation(NavigationScheme scheme, Qt::MouseButton button,
                   Qt::KeyboardModifiers mods) {
  for (const auto& b : bindings_for(scheme)) {
    if (b.button == button && b.mods == mods) return b.mode;
  }
  return DM::None;
}

QString navigation_scheme_key(NavigationScheme s) {
  switch (s) {
    case NavigationScheme::Cadly:     return QStringLiteral("cadly");
    case NavigationScheme::Blender:   return QStringLiteral("blender");
    case NavigationScheme::Rhino:     return QStringLiteral("rhino");
    case NavigationScheme::Fusion360: return QStringLiteral("fusion360");
    case NavigationScheme::Maya:      return QStringLiteral("maya");
  }
  return QStringLiteral("cadly");
}

NavigationScheme navigation_scheme_from_key(const QString& key) {
  for (const auto s : kAllNavigationSchemes) {
    if (navigation_scheme_key(s) == key) return s;
  }
  return NavigationScheme::Cadly;
}

QString navigation_scheme_name(NavigationScheme s) {
  switch (s) {
    case NavigationScheme::Blender:   return QStringLiteral("Blender");
    case NavigationScheme::Rhino:     return QStringLiteral("Rhino");
    case NavigationScheme::Fusion360: return QStringLiteral("Fusion 360");
    case NavigationScheme::Maya:      return QStringLiteral("Maya");
    case NavigationScheme::Cadly:     break;
  }
  return QCoreApplication::translate("NavigationScheme", "Cadly (default)");
}

QString navigation_scheme_legend(NavigationScheme s) {
  const auto rows = bindings_for(s);
  const auto join = [&rows](DM mode) {
    QStringList parts;
    for (const auto& b : rows) {
      if (b.mode == mode) parts << describe(b);
    }
    return parts.join(
      QCoreApplication::translate("NavigationScheme", " or "));
  };
  QStringList lines;
  lines << QCoreApplication::translate("NavigationScheme", "Orbit: %1")
             .arg(join(DM::Orbit));
  lines << QCoreApplication::translate("NavigationScheme", "Pan: %1")
             .arg(join(DM::Pan));
  const QString dolly = join(DM::Dolly);
  if (dolly.isEmpty()) {
    lines << QCoreApplication::translate(
      "NavigationScheme", "Zoom: scroll, anchored under the cursor");
  } else {
    lines << QCoreApplication::translate(
      "NavigationScheme",
      "Zoom: scroll (anchored under the cursor) or %1").arg(dolly);
  }
  lines << QCoreApplication::translate(
    "NavigationScheme", "Left click stays reserved for selection.");
  return lines.join(QLatin1Char('\n'));
}

} // namespace cadly::ui

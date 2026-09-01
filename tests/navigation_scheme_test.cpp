// Table checks for the navigation schemes: every scheme's advertised
// bindings resolve to the right drag mode, no scheme ever claims a plain
// left press (reserved for picking), the trackpad rows work everywhere,
// exact modifier matching holds, and the settings keys round-trip.

#include "cadly/ui/NavigationScheme.h"

#include <QtTest/QtTest>

using cadly::ui::CameraController;
using cadly::ui::NavigationScheme;
using cadly::ui::kAllNavigationSchemes;
using cadly::ui::navigation_scheme_from_key;
using cadly::ui::navigation_scheme_key;
using cadly::ui::navigation_scheme_legend;
using cadly::ui::resolve_navigation;
using DM = CameraController::DragMode;

class NavigationSchemeTest : public QObject {
  Q_OBJECT
private slots:
  void plain_left_never_resolves() {
    for (const auto s : kAllNavigationSchemes) {
      QCOMPARE(resolve_navigation(s, Qt::LeftButton, Qt::NoModifier),
               DM::None);
    }
  }

  void trackpad_rows_work_in_every_scheme() {
    for (const auto s : kAllNavigationSchemes) {
      QCOMPARE(resolve_navigation(s, Qt::LeftButton, Qt::AltModifier),
               DM::Orbit);
      QCOMPARE(resolve_navigation(s, Qt::LeftButton,
                                  Qt::AltModifier | Qt::ControlModifier),
               DM::Pan);
    }
  }

  void native_bindings_resolve() {
    QCOMPARE(resolve_navigation(NavigationScheme::Cadly,
                                Qt::RightButton, Qt::NoModifier), DM::Orbit);
    QCOMPARE(resolve_navigation(NavigationScheme::Cadly,
                                Qt::MiddleButton, Qt::NoModifier), DM::Pan);

    QCOMPARE(resolve_navigation(NavigationScheme::Blender,
                                Qt::MiddleButton, Qt::NoModifier), DM::Orbit);
    QCOMPARE(resolve_navigation(NavigationScheme::Blender,
                                Qt::MiddleButton, Qt::ShiftModifier), DM::Pan);
    QCOMPARE(resolve_navigation(NavigationScheme::Blender,
                                Qt::MiddleButton, Qt::ControlModifier),
             DM::Dolly);

    QCOMPARE(resolve_navigation(NavigationScheme::Rhino,
                                Qt::RightButton, Qt::NoModifier), DM::Orbit);
    QCOMPARE(resolve_navigation(NavigationScheme::Rhino,
                                Qt::RightButton, Qt::ShiftModifier), DM::Pan);
    QCOMPARE(resolve_navigation(NavigationScheme::Rhino,
                                Qt::RightButton, Qt::ControlModifier),
             DM::Dolly);

    QCOMPARE(resolve_navigation(NavigationScheme::Fusion360,
                                Qt::MiddleButton, Qt::NoModifier), DM::Pan);
    QCOMPARE(resolve_navigation(NavigationScheme::Fusion360,
                                Qt::MiddleButton, Qt::ShiftModifier),
             DM::Orbit);

    QCOMPARE(resolve_navigation(NavigationScheme::Maya,
                                Qt::LeftButton, Qt::AltModifier), DM::Orbit);
    QCOMPARE(resolve_navigation(NavigationScheme::Maya,
                                Qt::MiddleButton, Qt::AltModifier), DM::Pan);
    QCOMPARE(resolve_navigation(NavigationScheme::Maya,
                                Qt::RightButton, Qt::AltModifier), DM::Dolly);
  }

  void modifier_match_is_exact() {
    // Extra modifiers must not fall back onto a shorter row.
    QCOMPARE(resolve_navigation(NavigationScheme::Cadly, Qt::RightButton,
                                Qt::ShiftModifier), DM::None);
    QCOMPARE(resolve_navigation(NavigationScheme::Blender, Qt::MiddleButton,
                                Qt::ShiftModifier | Qt::ControlModifier),
             DM::None);
  }

  void settings_keys_round_trip() {
    for (const auto s : kAllNavigationSchemes) {
      QCOMPARE(navigation_scheme_from_key(navigation_scheme_key(s)), s);
    }
    QCOMPARE(navigation_scheme_from_key(QStringLiteral("nonsense")),
             NavigationScheme::Cadly);
  }

  void legend_mentions_every_mode() {
    for (const auto s : kAllNavigationSchemes) {
      const QString legend = navigation_scheme_legend(s);
      QVERIFY(legend.contains(QStringLiteral("Orbit")));
      QVERIFY(legend.contains(QStringLiteral("Pan")));
      QVERIFY(legend.contains(QStringLiteral("Zoom")));
    }
  }
};

QTEST_MAIN(NavigationSchemeTest)
#include "navigation_scheme_test.moc"

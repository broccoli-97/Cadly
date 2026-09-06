// Table checks for the navigation schemes: every scheme's advertised
// bindings resolve to the right drag mode, no scheme ever claims a plain
// left press (reserved for picking), the trackpad rows work everywhere,
// exact modifier matching holds, and the settings keys round-trip.

#include "cadly/scene/Camera.h"
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

  void turntable_keeps_up_and_clamps_elevation() {
    using cadly::scene::Camera;
    using cadly::scene::vec3;
    Camera camera;
    // Many small pitch steps past vertical: elevation must clamp short of
    // the pole instead of flipping over.
    for (int i = 0; i < 300; ++i) {
      camera.orbit_turntable(0.0f, glm::radians(1.0f), camera.target);
    }
    const float elevation = glm::degrees(
      std::asin(glm::dot(camera.forward(), vec3(0.0f, 1.0f, 0.0f))));
    QVERIFY(elevation <= 89.6f);
    QVERIFY(elevation >= 89.0f);
    // Yaw about world up must never tilt the horizon: camera right stays
    // horizontal through arbitrary yawing.
    Camera yawed;
    for (int i = 0; i < 50; ++i) {
      yawed.orbit_turntable(glm::radians(7.0f), glm::radians(0.5f),
                            yawed.target);
    }
    QVERIFY(std::abs(glm::dot(yawed.right(), vec3(0.0f, 1.0f, 0.0f)))
            < 1e-4f);
  }

  void orbit_style_keys_round_trip() {
    using OS = cadly::ui::CameraController::OrbitStyle;
    QCOMPARE(cadly::ui::orbit_style_from_key(
               cadly::ui::orbit_style_key(OS::Free)), OS::Free);
    QCOMPARE(cadly::ui::orbit_style_from_key(
               cadly::ui::orbit_style_key(OS::Turntable)), OS::Turntable);
    QCOMPARE(cadly::ui::orbit_style_from_key(QStringLiteral("junk")),
             OS::Free);
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

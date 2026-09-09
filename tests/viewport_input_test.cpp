#include "cadly/ui/CameraController.h"
#include "cadly/ui/ViewportWidget.h"
#include "cadly/input_qt/QtInputAdapter.h"
#include "cadly/renderer/SectionPlane.h"

#include <QFocusEvent>
#include <QHideEvent>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTest>
#include <QWheelEvent>

#include <cmath>

namespace cadly::ui {
using input::NavigationScheme;
namespace {
constexpr float kHalf = 1.0f;

void prepare_section_viewport(ViewportWidget& viewport) {
  viewport.resize(640, 480);
  auto scn = std::make_shared<scene::Scene>();
  scn->world_bounds.expand({-kHalf, -kHalf, -kHalf});
  scn->world_bounds.expand({kHalf, kHalf, kHalf});
  viewport.set_scene(scn);
  viewport.camera_controller()->set_viewport(640, 480);
  renderer::DisplayMode mode;
  mode.section_enabled = true;
  mode.section.normal = {1.0f, 0.0f, 0.0f};
  viewport.set_display_mode(mode);
}

void send_mouse(ViewportWidget& viewport, QEvent::Type type, QPoint pos,
                 Qt::MouseButton button, Qt::MouseButtons buttons,
                 Qt::KeyboardModifiers mods) {
  QMouseEvent event(type, QPointF(pos), QPointF(pos), button, buttons, mods);
  QCoreApplication::sendEvent(&viewport, &event);
}

void send_cancel(ViewportWidget& viewport, QEvent::Type type) {
  if (type == QEvent::FocusOut) {
    QFocusEvent event(type, Qt::OtherFocusReason);
    QCoreApplication::sendEvent(&viewport, &event);
  } else if (type == QEvent::Hide) {
    QHideEvent event;
    QCoreApplication::sendEvent(&viewport, &event);
  } else {
    QEvent event(type);
    QCoreApplication::sendEvent(&viewport, &event);
  }
}

Qt::MouseButton qt_button(input::Button button) {
  if (button == input::Button::Left) return Qt::LeftButton;
  if (button == input::Button::Middle) return Qt::MiddleButton;
  return Qt::RightButton;
}

} // namespace

class ViewportInputTest : public QObject {
  Q_OBJECT
private slots:
  void section_viewport_respects_navigation_scheme();
  void section_drag_keeps_priority_over_late_modifiers();
  void navigation_sequences_data();
  void navigation_sequences();
  void extra_buttons_do_not_steal_or_end_navigation();
  void late_modifiers_do_not_jump_or_switch_an_owned_action();
  void interrupted_gestures_data();
  void interrupted_gestures();
  void hidden_section_tool_cannot_keep_dragging();
  void document_switch_cancels_input_but_preserves_preferences();
  void section_rotation_keeps_shift_snapping_and_ownership();
  void wheel_reaches_camera_in_logical_coordinates();
};

void ViewportInputTest::section_viewport_respects_navigation_scheme() {
  struct Gesture {
    NavigationScheme scheme;
    Qt::MouseButton button;
    Qt::KeyboardModifiers modifiers;
    bool orbits;
  };
  for (const Gesture& gesture : {
         Gesture{NavigationScheme::Blender, Qt::MiddleButton,
                  Qt::NoModifier, true},
         Gesture{NavigationScheme::Fusion360, Qt::MiddleButton,
                  Qt::ShiftModifier, true},
         Gesture{NavigationScheme::Maya, Qt::RightButton,
                  Qt::AltModifier, false}}) {
    ViewportWidget viewport;
    prepare_section_viewport(viewport);
    viewport.set_input_preferences({gesture.scheme, input::OrbitStyle::Free});
    const scene::Camera before = viewport.camera_controller()->camera();
    send_mouse(viewport, QEvent::MouseButtonPress, {320, 240},
                 gesture.button, gesture.button, gesture.modifiers);
    send_mouse(viewport, QEvent::MouseMove, {360, 260}, Qt::NoButton,
                 gesture.button, gesture.modifiers);
    send_mouse(viewport, QEvent::MouseButtonRelease, {360, 260},
                 gesture.button, Qt::NoButton, gesture.modifiers);
    const scene::Camera& after = viewport.camera_controller()->camera();
    if (gesture.orbits) {
      QVERIFY(glm::length(after.forward() - before.forward()) > 1e-3f);
      QCOMPARE(after.distance, before.distance);
    } else {
      QVERIFY(std::abs(after.distance - before.distance) > 1e-3f);
      QVERIFY(glm::length(after.forward() - before.forward()) < 1e-6f);
    }
    QCOMPARE(viewport.display_mode().section.offset, 0.0f);
  }
}

void ViewportInputTest::section_drag_keeps_priority_over_late_modifiers() {
  ViewportWidget viewport;
  prepare_section_viewport(viewport);
  viewport.set_input_preferences({NavigationScheme::Maya, input::OrbitStyle::Free});
  const scene::Camera before = viewport.camera_controller()->camera();
  int plane_moves = 0;
  int background_clicks = 0;
  connect(&viewport, &ViewportWidget::section_plane_dragged, this,
          [&]() { ++plane_moves; });
  connect(&viewport, &ViewportWidget::background_clicked, this,
          [&]() { ++background_clicks; });

  send_mouse(viewport, QEvent::MouseButtonPress, {320, 240},
               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  send_mouse(viewport, QEvent::MouseMove, {350, 250}, Qt::NoButton,
               Qt::LeftButton, Qt::AltModifier);
  send_mouse(viewport, QEvent::MouseMove, {360, 260}, Qt::NoButton,
               Qt::LeftButton, Qt::AltModifier);
  send_mouse(viewport, QEvent::MouseButtonRelease, {360, 260},
               Qt::LeftButton, Qt::NoButton, Qt::AltModifier);

  QVERIFY(plane_moves > 0);
  QCOMPARE(background_clicks, 0);
  QVERIFY(std::abs(viewport.display_mode().section.offset) > 1e-3f);
  QVERIFY(!viewport.display_mode().show_rotation_pivot);
  const scene::Camera& after = viewport.camera_controller()->camera();
  QVERIFY(glm::length(after.forward() - before.forward()) < 1e-6f);
  QVERIFY(glm::length(after.target - before.target) < 1e-6f);
  QCOMPARE(after.distance, before.distance);
}


void ViewportInputTest::navigation_sequences_data() {
  QTest::addColumn<int>("scheme");
  QTest::addColumn<int>("button");
  QTest::addColumn<int>("modifiers");
  QTest::addColumn<int>("action");
  QTest::addColumn<int>("style");
  QTest::addColumn<bool>("perspective");
  QTest::addColumn<bool>("section");
  for (const auto& preset : input::navigation_presets()) {
    for (const auto& row : preset.bindings) {
      for (const auto style : {input::OrbitStyle::Free, input::OrbitStyle::Turntable}) {
        for (const bool perspective : {false, true}) {
          for (const bool section : {false, true}) {
            const auto mods = input_qt::modifiers_to_qt(row.modifiers);
            const auto name = QStringLiteral("%1-%2-%3-%4-%5-%6")
              .arg(QString::fromUtf8(preset.key.data(), static_cast<int>(preset.key.size())))
              .arg(static_cast<int>(row.button)).arg(mods.toInt()).arg(static_cast<int>(style))
              .arg(perspective).arg(section).toLatin1();
            QTest::newRow(name.constData()) << static_cast<int>(preset.scheme)
              << static_cast<int>(qt_button(row.button)) << static_cast<int>(mods.toInt())
              << static_cast<int>(row.action) << static_cast<int>(style) << perspective << section;
          }
        }
      }
    }
  }
}

void ViewportInputTest::navigation_sequences() {
  QFETCH(int, scheme);
  QFETCH(int, button);
  QFETCH(int, modifiers);
  QFETCH(int, action);
  QFETCH(int, style);
  QFETCH(bool, perspective);
  QFETCH(bool, section);
  ViewportWidget viewport;
  prepare_section_viewport(viewport);
  viewport.set_input_preferences({static_cast<NavigationScheme>(scheme), static_cast<input::OrbitStyle>(style)});
  auto mode = viewport.display_mode();
  mode.section_enabled = section;
  viewport.set_display_mode(mode);
  auto* controller = viewport.camera_controller();
  controller->set_projection(perspective ? scene::Projection::Perspective : scene::Projection::Orthographic);
  auto expected = controller->camera();
  const auto operation = static_cast<input::NavigationAction>(action);
  if (operation == input::NavigationAction::Orbit) {
    if (static_cast<input::OrbitStyle>(style) == input::OrbitStyle::Turntable) {
      expected.orbit_turntable(-0.24f, -0.12f, expected.target);
    } else {
      expected.orbit(-0.24f, -0.12f, expected.target);
    }
  } else if (operation == input::NavigationAction::Pan) {
    expected.target -= expected.right() * (0.1f * expected.distance);
    expected.target += expected.up() * (0.05f * expected.distance);
  } else {
    expected.distance *= 1.05f;
  }
  QSignalSpy background(&viewport, &ViewportWidget::background_clicked);
  const auto btn = static_cast<Qt::MouseButton>(button);
  const auto mods = Qt::KeyboardModifiers::fromInt(modifiers);
  send_mouse(viewport, QEvent::MouseButtonPress, {320, 240}, btn, btn, mods);
  QCOMPARE(controller->is_rotating(), operation == input::NavigationAction::Orbit);
  send_mouse(viewport, QEvent::MouseMove, {360, 260}, Qt::NoButton, btn, mods);
  send_mouse(viewport, QEvent::MouseButtonRelease, {360, 260}, btn, Qt::NoButton, mods);
  const auto& actual = controller->camera();
  QVERIFY(glm::length(actual.target - expected.target) < 1e-5f);
  QVERIFY(glm::length(actual.forward() - expected.forward()) < 1e-5f);
  QVERIFY(std::abs(actual.distance - expected.distance) < 1e-5f);
  QVERIFY(!controller->is_rotating() && !viewport.display_mode().show_rotation_pivot);
  QCOMPARE(background.count(), 0);
  QCOMPARE(viewport.display_mode().section.offset, 0.0f);
  QCOMPARE(viewport.display_mode().section_enabled, section);
}

void ViewportInputTest::extra_buttons_do_not_steal_or_end_navigation() {
  ViewportWidget viewport;
  prepare_section_viewport(viewport);
  auto* controller = viewport.camera_controller();
  const auto before = controller->camera();
  QSignalSpy background(&viewport, &ViewportWidget::background_clicked);
  send_mouse(viewport, QEvent::MouseButtonPress, {320, 240}, Qt::RightButton, Qt::RightButton, Qt::NoModifier);
  send_mouse(viewport, QEvent::MouseButtonPress, {320, 240}, Qt::LeftButton,
             Qt::LeftButton | Qt::RightButton, Qt::NoModifier);
  send_mouse(viewport, QEvent::MouseButtonRelease, {320, 240}, Qt::LeftButton, Qt::RightButton, Qt::NoModifier);
  QVERIFY(controller->is_rotating());
  send_mouse(viewport, QEvent::MouseMove, {360, 260}, Qt::NoButton, Qt::RightButton, Qt::NoModifier);
  QVERIFY(glm::length(controller->camera().forward() - before.forward()) > 1e-3f);
  QCOMPARE(viewport.display_mode().section.offset, 0.0f);
  QCOMPARE(background.count(), 0);
  send_mouse(viewport, QEvent::MouseButtonRelease, {360, 260}, Qt::RightButton, Qt::NoButton, Qt::NoModifier);
  QVERIFY(!controller->is_rotating());
}

void ViewportInputTest::late_modifiers_do_not_jump_or_switch_an_owned_action() {
  ViewportWidget viewport;
  prepare_section_viewport(viewport);
  auto* controller = viewport.camera_controller();
  const auto before = controller->camera();
  QSignalSpy background(&viewport, &ViewportWidget::background_clicked);
  send_mouse(viewport, QEvent::MouseButtonPress, {10, 10}, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  send_mouse(viewport, QEvent::MouseMove, {100, 100}, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
  send_mouse(viewport, QEvent::MouseMove, {200, 200}, Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
  QVERIFY(controller->is_rotating());
  QVERIFY(glm::length(controller->camera().forward() - before.forward()) < 1e-6f);
  send_mouse(viewport, QEvent::MouseMove, {220, 220}, Qt::NoButton, Qt::LeftButton,
             Qt::AltModifier | Qt::ControlModifier);
  QVERIFY(glm::length(controller->camera().forward() - before.forward()) > 1e-3f);
  QVERIFY(glm::length(controller->camera().target - before.target) < 1e-6f);
  QCOMPARE(background.count(), 1);  // preserve the existing left-press selection contract
}

void ViewportInputTest::interrupted_gestures_data() {
  QTest::addColumn<int>("event_type");
  QTest::addColumn<bool>("tool");
  for (const auto type : {QEvent::FocusOut, QEvent::WindowDeactivate, QEvent::UngrabMouse, QEvent::Hide}) {
    for (const bool tool : {false, true}) {
      const auto name = QStringLiteral("%1-%2").arg(static_cast<int>(type)).arg(tool).toLatin1();
      QTest::newRow(name.constData()) << static_cast<int>(type) << tool;
    }
  }
}

void ViewportInputTest::interrupted_gestures() {
  QFETCH(int, event_type);
  QFETCH(bool, tool);
  ViewportWidget viewport;
  prepare_section_viewport(viewport);
  auto* controller = viewport.camera_controller();
  QSignalSpy finished(&viewport, &ViewportWidget::section_drag_finished);
  const auto button = tool ? Qt::LeftButton : Qt::RightButton;
  send_mouse(viewport, QEvent::MouseButtonPress, {320, 240}, button, button, Qt::NoModifier);
  send_mouse(viewport, QEvent::MouseMove, {350, 250}, Qt::NoButton, button, Qt::NoModifier);
  send_cancel(viewport, static_cast<QEvent::Type>(event_type));
  send_cancel(viewport, static_cast<QEvent::Type>(event_type));
  const auto after = controller->camera();
  const float offset = viewport.display_mode().section.offset;
  QVERIFY(!controller->is_rotating() && !viewport.display_mode().show_rotation_pivot);
  QCOMPARE(viewport.cursor().shape(), Qt::ArrowCursor);
  QCOMPARE(finished.count(), tool ? 1 : 0);
  send_mouse(viewport, QEvent::MouseMove, {390, 280}, Qt::NoButton, button, Qt::AltModifier);
  QVERIFY(glm::length(controller->camera().forward() - after.forward()) < 1e-6f);
  QCOMPARE(viewport.display_mode().section.offset, offset);
  // A fresh press starts normally even if focus transfer lost the old release.
  send_mouse(viewport, QEvent::MouseButtonPress, {100, 100}, Qt::RightButton, Qt::RightButton, Qt::NoModifier);
  QVERIFY(controller->is_rotating());
}

void ViewportInputTest::hidden_section_tool_cannot_keep_dragging() {
  for (const bool disable_section : {false, true}) {
    ViewportWidget viewport;
    prepare_section_viewport(viewport);
    QSignalSpy finished(&viewport, &ViewportWidget::section_drag_finished);
    send_mouse(viewport, QEvent::MouseButtonPress, {320, 240}, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    auto mode = viewport.display_mode();
    if (disable_section) mode.section_enabled = false;
    else mode.section_show_plane = false;
    viewport.set_display_mode(mode);
    QCOMPARE(finished.count(), 1);
    QCOMPARE(viewport.display_mode().section_hot_part, renderer::SectionGizmoPart::None);
    send_mouse(viewport, QEvent::MouseMove, {390, 270}, Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
    QCOMPARE(viewport.display_mode().section.offset, 0.0f);
    QVERIFY(!viewport.camera_controller()->is_rotating());
  }
}

void ViewportInputTest::document_switch_cancels_input_but_preserves_preferences() {
  ViewportWidget viewport;
  prepare_section_viewport(viewport);
  viewport.set_input_preferences({NavigationScheme::Maya, input::OrbitStyle::Turntable});
  send_mouse(viewport, QEvent::MouseButtonPress, {320, 240}, Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
  QVERIFY(viewport.camera_controller()->is_rotating());
  auto replacement = std::make_shared<scene::Scene>();
  replacement->world_bounds.expand({-2.0f, -2.0f, -2.0f});
  replacement->world_bounds.expand({2.0f, 2.0f, 2.0f});
  replacement->camera.target = {3.0f, 4.0f, 5.0f};
  replacement->camera.distance = 40.0f;
  viewport.set_scene(replacement, false);
  QVERIFY(!viewport.camera_controller()->is_rotating());
  send_mouse(viewport, QEvent::MouseMove, {390, 270}, Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
  const auto& camera = viewport.camera_controller()->camera();
  QVERIFY(glm::length(camera.target - replacement->camera.target) < 1e-6f);
  QVERIFY(glm::length(camera.forward() - replacement->camera.forward()) < 1e-6f);
  QCOMPARE(camera.distance, replacement->camera.distance);
  QCOMPARE(viewport.input_preferences().navigation_scheme, NavigationScheme::Maya);
  QCOMPARE(viewport.input_preferences().orbit_style, input::OrbitStyle::Turntable);
  // Clearing a document while a tool owns the pointer must not leave a drag
  // that dereferences the old scene's bounds on the next move.
  send_mouse(viewport, QEvent::MouseButtonRelease, {390, 270}, Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
  prepare_section_viewport(viewport);
  send_mouse(viewport, QEvent::MouseButtonPress, {320, 240}, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  viewport.set_scene(nullptr);
  send_mouse(viewport, QEvent::MouseMove, {390, 270}, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
}

void ViewportInputTest::section_rotation_keeps_shift_snapping_and_ownership() {
  ViewportWidget viewport;
  prepare_section_viewport(viewport);
  auto* controller = viewport.camera_controller();
  controller->set_view(0.0f, 0.0f);
  const auto before = controller->camera();
  scene::Aabb bounds;
  bounds.expand({-kHalf, -kHalf, -kHalf});
  bounds.expand({kHalf, kHalf, kHalf});
  scene::vec3 centre{0.0f}, e0{0.0f}, e1{0.0f};
  QVERIFY(renderer::section_ring_frame(viewport.display_mode().section, bounds,
    renderer::SectionGizmoPart::RotateZ,
    controller->world_per_logical_pixel() / static_cast<float>(viewport.devicePixelRatioF()),
    centre, e0, e1));
  const auto pixel = [&](float angle) {
    const auto p = controller->project_to_screen(renderer::section_ring_point(centre, e0, e1, angle));
    return QPoint(qRound(p.x), qRound(p.y));
  };
  const QPoint start = pixel(glm::radians(45.0f));
  const QPoint end = pixel(glm::radians(68.0f));
  float degrees = 0.0f;
  renderer::SectionGizmoPart moved = renderer::SectionGizmoPart::None;
  connect(&viewport, &ViewportWidget::section_plane_dragged, this,
    [&](renderer::SectionPlane, renderer::SectionGizmoPart part, float angle) {
      moved = part;
      degrees = angle;
    });
  send_mouse(viewport, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  send_mouse(viewport, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton,
             Qt::ShiftModifier | Qt::AltModifier);
  QCOMPARE(moved, renderer::SectionGizmoPart::RotateZ);
  QVERIFY(std::abs(degrees) > 1.0f);
  QVERIFY(std::abs(degrees / 15.0f - std::round(degrees / 15.0f)) < 1e-4f);
  send_mouse(viewport, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
  QVERIFY(std::abs(std::abs(degrees) - 23.0f) < 2.0f);
  QVERIFY(glm::length(controller->camera().forward() - before.forward()) < 1e-6f);
  QVERIFY(!controller->is_rotating());
}

void ViewportInputTest::wheel_reaches_camera_in_logical_coordinates() {
  ViewportWidget viewport;
  prepare_section_viewport(viewport);
  CameraController expected;
  expected.set_viewport(640, 480);
  expected.restore_camera(viewport.camera_controller()->camera(), {-kHalf, -kHalf, -kHalf}, {kHalf, kHalf, kHalf});
  expected.apply_navigation(input::scroll_zoom({410, 190}, -120));
  QWheelEvent wheel(QPointF(410, 190), QPointF(1400, 1000), QPoint(0, 30), QPoint(0, -120),
                    Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, true);
  QCoreApplication::sendEvent(&viewport, &wheel);
  const auto& actual = viewport.camera_controller()->camera();
  QVERIFY(glm::length(actual.target - expected.camera().target) < 1e-6f);
  QCOMPARE(actual.distance, expected.camera().distance);
}

} // namespace cadly::ui

QTEST_MAIN(cadly::ui::ViewportInputTest)
#include "viewport_input_test.moc"

#include "InspectorWidget.h"

#include <QAbstractButton>
#include <QSignalSpy>
#include <QTest>

namespace cadly::ui {

class InspectorTest : public QObject {
  Q_OBJECT

private slots:
  void display_reset_restores_struct_defaults();
  void import_reset_restores_backend_defaults();
};

// Clicking the Display pane's "Reset to Defaults" must restore the
// DisplayMode struct defaults (the single source of truth) and announce the
// change exactly once — load_display() blocks the per-control signals, so a
// missing manual emit would silently skip persistence and the viewport.
void InspectorTest::display_reset_restores_struct_defaults() {
  InspectorWidget inspector;
  inspector.resize(320, 640);
  inspector.show();
  QVERIFY(QTest::qWaitForWindowExposed(&inspector));
  inspector.set_current_tab(InspectorWidget::DisplayTab);

  renderer::DisplayMode custom;
  custom.edge_intensity = 1.0f;
  custom.msaa_samples   = 8;
  custom.show_scale_bar = false;
  custom.show_axes      = false;
  inspector.load_display(custom);

  renderer::DisplayMode readback;
  inspector.apply_display(readback);
  QCOMPARE(readback.msaa_samples, 8);
  QVERIFY(!readback.show_scale_bar);

  auto* reset = inspector.findChild<QAbstractButton*>(
    QStringLiteral("inspector_display_reset"));
  QVERIFY(reset != nullptr);
  QSignalSpy changed(&inspector, &InspectorWidget::display_changed);
  QTest::mouseClick(reset, Qt::LeftButton);
  QCOMPARE(changed.count(), 1);

  const renderer::DisplayMode defaults;
  inspector.apply_display(readback);
  QCOMPARE(readback.edge_intensity, defaults.edge_intensity);
  QCOMPARE(readback.msaa_samples,   defaults.msaa_samples);
  QCOMPARE(readback.show_scale_bar, defaults.show_scale_bar);
  QCOMPARE(readback.show_axes,      defaults.show_axes);
}

// Same contract for the Import pane: back to ImportOptions{} (the backend
// defaults the CLI shares), one import_options_changed emission for the
// owner to persist, and the "review before each import" workflow preference
// left untouched — it is not an import option.
void InspectorTest::import_reset_restores_backend_defaults() {
  InspectorWidget inspector;
  inspector.resize(320, 640);
  inspector.show();
  QVERIFY(QTest::qWaitForWindowExposed(&inspector));
  inspector.set_current_tab(InspectorWidget::ImportTab);

  cad::ImportOptions custom;
  custom.tessellation_mode = cad::TessellationMode::Absolute;
  custom.linear_deflection = 0.5;
  custom.parallel_meshing  = false;
  custom.load_colors       = false;
  inspector.set_import_options(custom);
  inspector.set_review_before_import(true);

  auto* reset = inspector.findChild<QAbstractButton*>(
    QStringLiteral("inspector_import_reset"));
  QVERIFY(reset != nullptr);
  QSignalSpy changed(&inspector, &InspectorWidget::import_options_changed);
  QTest::mouseClick(reset, Qt::LeftButton);
  QCOMPARE(changed.count(), 1);

  const cad::ImportOptions defaults;
  const cad::ImportOptions result = inspector.import_options();
  QVERIFY(result.tessellation_mode == defaults.tessellation_mode);
  QCOMPARE(result.linear_deflection, defaults.linear_deflection);
  QCOMPARE(result.parallel_meshing,  defaults.parallel_meshing);
  QCOMPARE(result.load_colors,       defaults.load_colors);
  QVERIFY(inspector.review_before_import());
}

} // namespace cadly::ui

QTEST_MAIN(cadly::ui::InspectorTest)

#include "inspector_test.moc"

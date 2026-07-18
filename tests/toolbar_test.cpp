#include "SegmentedControl.h"
#include "ToolTip.h"
#include "ToolbarButton.h"
#include "ToolbarWidget.h"

#include <QAction>
#include <QApplication>
#include <QHelpEvent>
#include <QMenu>
#include <QSignalSpy>
#include <QTest>
#include <QToolTip>

namespace cadly::ui {

namespace {

// The QTipLabel QToolTip::showText creates is a top-level widget; find the
// visible one (or null).
QWidget* visible_tip_label() {
  for (auto* top_level : QApplication::topLevelWidgets()) {
    if (top_level->inherits("QTipLabel") && top_level->isVisible()) {
      return top_level;
    }
  }
  return nullptr;
}

} // namespace

class ToolbarTest : public QObject {
  Q_OBJECT

private slots:
  void shaded_menu_contains_overlay_actions();
  void long_tooltips_are_width_bounded();
  void rendered_long_tooltip_stays_local();
  void app_filter_keeps_tooltip_property_plain();
};

void ToolbarTest::shaded_menu_contains_overlay_actions() {
  QAction open;
  QAction fit;
  QAction edges;
  QAction mesh;
  QAction perspective;
  QAction sidebar;
  QAction inspector;
  QAction strip;
  QAction zero_chrome;
  QAction theme;
  QMenu recents;

  edges.setCheckable(true);
  edges.setChecked(true);
  mesh.setCheckable(true);

  ToolbarWidget::Actions actions;
  actions.open = &open;
  actions.recents_menu = &recents;
  actions.fit = &fit;
  actions.edges = &edges;
  actions.triangle_mesh = &mesh;
  actions.perspective = &perspective;
  actions.toggle_sidebar = &sidebar;
  actions.toggle_inspector = &inspector;
  actions.toggle_strip = &strip;
  actions.zero_chrome = &zero_chrome;
  actions.theme = &theme;

  ToolbarWidget toolbar(actions);
  toolbar.show();
  QVERIFY(QTest::qWaitForWindowExposed(&toolbar));

  auto* segments = toolbar.display_segments();
  QCOMPARE(segments->count(), 3);
  QMenu* menu = segments->segment_menu(0);
  QVERIFY(menu != nullptr);
  QCOMPARE(menu->actions(), QList<QAction*>({&edges, &mesh}));

  for (auto* button : toolbar.findChildren<ToolbarButton*>()) {
    QVERIFY(button->defaultAction() != &edges);
    QVERIFY(button->defaultAction() != &mesh);
  }

  segments->set_current(2);
  QSignalSpy changed(segments, &SegmentedControl::segment_clicked);
  segments->show_segment_menu(0);
  QCOMPARE(segments->current(), 0);
  QCOMPARE(changed.count(), 1);
  QCOMPARE(changed.front().front().toInt(), 0);
  QTRY_VERIFY(menu->isVisible());
  menu->close();
}

void ToolbarTest::long_tooltips_are_width_bounded() {
  const QString short_text = QStringLiteral("Recent files");
  QCOMPARE(bounded_tooltip(short_text), short_text);

  const QString long_text = QStringLiteral(
    "Merge coincident vertices within each face. Preserves seams and may "
    "increase import time.");
  const QString result = bounded_tooltip(long_text);
  QVERIFY(result.startsWith(QLatin1String("<qt>")));
  QVERIFY(result.contains(QLatin1String("width=\"240\"")));
  QVERIFY(result.contains(long_text));

  // Short but tag-like text must be escaped explicitly: Qt::AutoText would
  // otherwise sniff it as rich text and silently swallow the tag characters
  // (angle brackets are legal in Linux file names).
  const QString tag_like = QStringLiteral("/tmp/a<b>.step");
  const QString escaped = bounded_tooltip(tag_like);
  QVERIFY(escaped.startsWith(QLatin1String("<qt>")));
  QVERIFY(escaped.contains(QLatin1String("&lt;b&gt;")));
}

void ToolbarTest::rendered_long_tooltip_stays_local() {
  QWidget anchor;
  anchor.resize(300, 120);
  anchor.show();
  QVERIFY(QTest::qWaitForWindowExposed(&anchor));

  const QString text = bounded_tooltip(QStringLiteral(
    "Merge coincident vertices within each face. Preserves seams; may "
    "increase import time."));
  QToolTip::showText(anchor.mapToGlobal(QPoint(150, 60)), text, &anchor);

  QWidget* tip = nullptr;
  QTRY_VERIFY_WITH_TIMEOUT((tip = visible_tip_label()) != nullptr, 1000);
  QVERIFY(tip->width() <= 260);
  QVERIFY(tip->height() > tip->fontMetrics().height());
  QToolTip::hideText();
  QTRY_VERIFY_WITH_TIMEOUT(visible_tip_label() == nullptr, 1000);

  // An unbreakable run (a Windows path: no spaces, no UAX-14 break points)
  // must not expand the fixed-width table past the bound — QTextDocument
  // treats the table width as preferred, so bounded_tooltip has to insert
  // its own break opportunities.
  const QString path = bounded_tooltip(QStringLiteral(
    "C:\\Users\\pacer\\Documents\\CAD\\turbine_housing_rev_D_2026_final.step"));
  QToolTip::showText(anchor.mapToGlobal(QPoint(150, 60)), path, &anchor);
  tip = nullptr;
  QTRY_VERIFY_WITH_TIMEOUT((tip = visible_tip_label()) != nullptr, 1000);
  QVERIFY(tip->width() <= 260);
  QToolTip::hideText();
}

void ToolbarTest::app_filter_keeps_tooltip_property_plain() {
  install_bounded_tooltips();

  QWidget target;
  target.resize(300, 120);
  const QString plain = QStringLiteral(
    "Merge coincident vertices within each face. Preserves seams; may "
    "increase import time.");
  target.setToolTip(plain);
  target.show();
  QVERIFY(QTest::qWaitForWindowExposed(&target));

  // The stored property must stay plain: Qt accessibility reports
  // toolTip() verbatim as the control's Description, so markup here would
  // be announced by screen readers.
  QCOMPARE(target.toolTip(), plain);

  // Route a real ToolTip event through the application-level filter and
  // check the popup comes out bounded.
  QHelpEvent help(QEvent::ToolTip, QPoint(150, 60),
                  target.mapToGlobal(QPoint(150, 60)));
  QApplication::sendEvent(&target, &help);

  QWidget* tip = nullptr;
  QTRY_VERIFY_WITH_TIMEOUT((tip = visible_tip_label()) != nullptr, 1000);
  QVERIFY(tip->width() <= 260);
  QToolTip::hideText();
}

} // namespace cadly::ui

QTEST_MAIN(cadly::ui::ToolbarTest)

#include "toolbar_test.moc"

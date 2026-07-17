#include "SegmentedControl.h"
#include "ToolbarButton.h"
#include "ToolbarWidget.h"

#include <QAction>
#include <QMenu>
#include <QSignalSpy>
#include <QTest>

namespace cadly::ui {

class ToolbarTest : public QObject {
  Q_OBJECT

private slots:
  void shaded_menu_contains_overlay_actions();
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

} // namespace cadly::ui

QTEST_MAIN(cadly::ui::ToolbarTest)

#include "toolbar_test.moc"

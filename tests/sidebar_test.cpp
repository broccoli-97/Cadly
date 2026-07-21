#include "SidebarWidget.h"

#include <QSignalSpy>
#include <QTest>

#include <memory>

namespace cadly::ui {

namespace {

// Minimal two-level assembly:
//   0 asm ─┬─ 1 sub ── 2 bolt (mesh 0)
//          └─ 3 plate (mesh 1)
std::shared_ptr<scene::Scene> make_scene() {
  auto s = std::make_shared<scene::Scene>();
  s->meshes.push_back(std::make_shared<scene::Mesh>());
  s->meshes.push_back(std::make_shared<scene::Mesh>());

  scene::Node root;  root.name = "asm";
  scene::Node sub;   sub.name = "sub";     sub.parent = 0;
  scene::Node bolt;  bolt.name = "bolt";   bolt.parent = 1;  bolt.mesh_index = 0;
  scene::Node plate; plate.name = "plate"; plate.parent = 0; plate.mesh_index = 1;
  root.children = {1, 3};
  sub.children  = {2};
  s->nodes = {root, sub, bolt, plate};
  return s;
}

} // namespace

class SidebarTest : public QObject {
  Q_OBJECT

private slots:
  void selecting_a_group_highlights_its_subtree();
  void clear_selection_cancels_the_highlight();
  void isolate_ghosts_everything_outside_the_focus();
  void isolate_root_carries_no_selection_tint();
  void scene_handover_resets_isolate();
};

// A tree pick must flag the picked node AND its descendants (the viewport
// highlight follows Node::selected per node, so a group pick has to push the
// flag down), clear every other node, and announce both the pick and the
// repaint.
void SidebarTest::selecting_a_group_highlights_its_subtree() {
  SidebarWidget sidebar;
  auto scene = make_scene();
  sidebar.set_scene(scene);

  QSignalSpy picked(&sidebar, &SidebarWidget::node_selected);
  QSignalSpy repaint(&sidebar, &SidebarWidget::highlight_changed);
  sidebar.select_node(1);   // the "sub" group

  QCOMPARE(picked.count(), 1);
  QCOMPARE(picked.first().first().toUInt(), 1u);
  QVERIFY(repaint.count() >= 1);
  QVERIFY(!scene->nodes[0].selected);
  QVERIFY(scene->nodes[1].selected);
  QVERIFY(scene->nodes[2].selected);   // descendant follows the group
  QVERIFY(!scene->nodes[3].selected);

  sidebar.select_node(3);   // moving the pick must clear the old subtree
  QVERIFY(!scene->nodes[1].selected);
  QVERIFY(!scene->nodes[2].selected);
  QVERIFY(scene->nodes[3].selected);
}

// Cancelling the highlight — viewport background clicks and tree blank-area
// clicks both funnel into clear_selection — must wipe Node::selected on the
// whole scene, announce a repaint, and report kInvalid so the Properties tab
// resets instead of pinning the last part. A second clear stays quiet.
void SidebarTest::clear_selection_cancels_the_highlight() {
  SidebarWidget sidebar;
  auto scene = make_scene();
  sidebar.set_scene(scene);
  sidebar.select_node(1);
  QVERIFY(scene->nodes[1].selected);

  QSignalSpy picked(&sidebar, &SidebarWidget::node_selected);
  QSignalSpy repaint(&sidebar, &SidebarWidget::highlight_changed);
  sidebar.clear_selection();

  QCOMPARE(picked.count(), 1);
  QCOMPARE(picked.first().first().toUInt(), scene::Node::kInvalid);
  QVERIFY(repaint.count() >= 1);
  for (const auto& n : scene->nodes) QVERIFY(!n.selected);

  picked.clear();
  sidebar.clear_selection();
  QCOMPARE(picked.count(), 0);
}

// Isolate = ghost everything except the focus subtree and its ancestors
// (containers of the focus must not read as "elsewhere"); exiting restores
// the full assembly. kInvalid on the signal is the banner-hide contract.
void SidebarTest::isolate_ghosts_everything_outside_the_focus() {
  SidebarWidget sidebar;
  auto scene = make_scene();
  sidebar.set_scene(scene);

  QSignalSpy changed(&sidebar, &SidebarWidget::isolate_changed);
  sidebar.set_isolate(2);   // the bolt

  QCOMPARE(changed.count(), 1);
  QCOMPARE(changed.first().first().toUInt(), 2u);
  QCOMPARE(sidebar.isolate_node(), 2u);
  QVERIFY(!scene->nodes[0].ghosted);   // ancestor
  QVERIFY(!scene->nodes[1].ghosted);   // ancestor
  QVERIFY(!scene->nodes[2].ghosted);   // focus
  QVERIFY(scene->nodes[3].ghosted);    // everything else

  sidebar.set_isolate(scene::Node::kInvalid);
  QCOMPARE(changed.count(), 2);
  QCOMPARE(changed.last().first().toUInt(), scene::Node::kInvalid);
  for (const auto& n : scene->nodes) QVERIFY(!n.ghosted);
}

// The isolated part is shown "normally" — double-clicking selects AND
// isolates the same node, and the selection tint must not wash over the very
// part the user isolated. Leaving isolate re-derives the tint for the still-
// current row.
void SidebarTest::isolate_root_carries_no_selection_tint() {
  SidebarWidget sidebar;
  auto scene = make_scene();
  sidebar.set_scene(scene);

  sidebar.select_node(2);
  QVERIFY(scene->nodes[2].selected);
  sidebar.set_isolate(2);
  QVERIFY(!scene->nodes[2].selected);
  sidebar.set_isolate(scene::Node::kInvalid);
  QVERIFY(scene->nodes[2].selected);
}

// A scene handover (tab switch, re-import) starts the tree fresh: stale
// viewer flags on the incoming scene are wiped and an active isolate is
// reported as exited — per-document restore is the shell's job, via
// set_isolate after set_scene.
void SidebarTest::scene_handover_resets_isolate() {
  SidebarWidget sidebar;
  auto scene = make_scene();
  sidebar.set_scene(scene);
  sidebar.set_isolate(3);
  QVERIFY(scene->nodes[1].ghosted);

  QSignalSpy changed(&sidebar, &SidebarWidget::isolate_changed);
  sidebar.set_scene(scene);   // same document handed back
  QCOMPARE(changed.count(), 1);
  QCOMPARE(changed.first().first().toUInt(), scene::Node::kInvalid);
  QCOMPARE(sidebar.isolate_node(), scene::Node::kInvalid);
  for (const auto& n : scene->nodes) {
    QVERIFY(!n.ghosted);
    QVERIFY(!n.selected);
  }

  sidebar.set_isolate(3);   // the shell's restore path
  QVERIFY(scene->nodes[1].ghosted);
  QVERIFY(!scene->nodes[3].ghosted);
}

} // namespace cadly::ui

QTEST_MAIN(cadly::ui::SidebarTest)

#include "sidebar_test.moc"

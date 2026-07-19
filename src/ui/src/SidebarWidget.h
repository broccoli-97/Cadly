#pragma once

// Left sidebar: source-list model tree with filter field, per-node
// visibility eyes (⌥-click to solo a subtree), and a hover ⓘ that opens a
// pinnable Get Info popover. Selecting a row highlights the part in the
// viewport (Node::selected); double-clicking a row isolates it — everything
// else ghosts in the viewport (Node::ghosted) and grays out in the tree
// until set_isolate(kInvalid) brings the assembly back. Replaces
// ModelTreeDock. Private to the ui module.

#include "cadly/scene/Scene.h"

#include <QWidget>

#include <cstdint>
#include <memory>

class QLineEdit;
class QSortFilterProxyModel;
class QStandardItem;
class QStandardItemModel;
class QTreeView;

namespace cadly::ui {

class SidebarWidget : public QWidget {
  Q_OBJECT
public:
  explicit SidebarWidget(QWidget* parent = nullptr);

  void set_scene(std::shared_ptr<scene::Scene> scene);
  void clear();

  // Make a node the tree's current row (as if clicked): selection pill,
  // Properties tab, viewport highlight. Used by the dev/test demo hook.
  void select_node(std::uint32_t node_index);

  // Enter/leave isolate mode. kInvalid exits. Recomputes Node::ghosted for
  // the whole scene, grays the tree, and emits isolate_changed — the shell
  // owns the exit affordance (floating Back pill) and per-document
  // persistence, so this is also the restore entry point on tab switches.
  void set_isolate(std::uint32_t node_index);
  std::uint32_t isolate_node() const { return isolate_node_; }

  // Open the pinnable Get Info popover for a node, anchored to a global rect.
  // Normally invoked by the row delegate's (i) glyph; also reachable from the
  // dev/test demo hook.
  void show_get_info_for(std::uint32_t node_index, const QRect& anchor_global) {
    show_get_info(node_index, anchor_global);
  }

signals:
  // Selection changed (click / keyboard) — drives the Properties tab.
  void node_selected(std::uint32_t node_index);
  // One or more nodes changed visibility — owner repaints the viewport.
  void visibility_changed();
  // Node::selected flags changed — owner repaints the viewport.
  void highlight_changed();
  // Isolate mode entered/left/retargeted. kInvalid means "back to full
  // assembly". Owner shows/hides the Back pill and repaints the viewport.
  void isolate_changed(std::uint32_t isolate_node);

protected:
  void paintEvent(QPaintEvent*) override;

private:
  friend class SidebarDelegate;

  void rebuild();
  // Set `visible` on a node and its whole subtree. CAD assemblies expect a
  // group's eye to take its children with it; the renderer tests each node
  // independently, so the flag has to be pushed down explicitly.
  void set_subtree_visible(std::uint32_t node_index, bool visible);
  void toggle_eye(std::uint32_t node_index, bool solo);
  // Recompute Node::selected for the whole scene from the tree's current
  // row. Same push-down rule as visibility: picking a group highlights the
  // group's parts. The isolate root itself is deliberately NOT flagged —
  // in isolate mode that part is the one being shown "normally".
  void apply_selection(std::uint32_t node_index);
  void set_subtree_selected(std::uint32_t node_index, bool selected);
  void set_subtree_ghosted(std::uint32_t node_index, bool ghosted);
  void show_get_info(std::uint32_t node_index, const QRect& row_rect_global);
  // Mirror per-node scene flags (visible / ghosted) into the item roles the
  // delegate paints from.
  void sync_node_flags(QStandardItem* item);

  QTreeView*             tree_{nullptr};
  QStandardItemModel*    model_{nullptr};
  QSortFilterProxyModel* proxy_{nullptr};
  QLineEdit*             filter_{nullptr};

  std::shared_ptr<scene::Scene> scene_;
  // kInvalid when no solo is active; otherwise the node whose subtree is the
  // only visible part of the scene (⌥-click the same eye again to restore).
  std::uint32_t solo_node_{scene::Node::kInvalid};
  // Tree's current row as a scene node index; drives Node::selected.
  std::uint32_t selected_node_{scene::Node::kInvalid};
  // kInvalid when the full assembly is showing; otherwise the isolate focus.
  std::uint32_t isolate_node_{scene::Node::kInvalid};
};

} // namespace cadly::ui

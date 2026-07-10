#pragma once

// Left sidebar: source-list model tree with filter field, per-node
// visibility eyes (⌥-click to solo a subtree), and a hover ⓘ that opens a
// pinnable Get Info popover. Replaces ModelTreeDock. Private to the ui
// module.

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
  void show_get_info(std::uint32_t node_index, const QRect& row_rect_global);
  void sync_visible_flags(QStandardItem* item);

  QTreeView*             tree_{nullptr};
  QStandardItemModel*    model_{nullptr};
  QSortFilterProxyModel* proxy_{nullptr};
  QLineEdit*             filter_{nullptr};

  std::shared_ptr<scene::Scene> scene_;
  // kInvalid when no solo is active; otherwise the node whose subtree is the
  // only visible part of the scene (⌥-click the same eye again to restore).
  std::uint32_t solo_node_{scene::Node::kInvalid};
};

} // namespace cadly::ui

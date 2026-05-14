#pragma once

#include "cadly/scene/Scene.h"

#include <QDockWidget>
#include <memory>

class QTreeView;
class QStandardItemModel;
class QStandardItem;

namespace cadly::ui {

// Tree view of the imported assembly. Each row maps to a scene::Node by index
// stored in Qt::UserRole + 1. Emits a signal when a node is selected.
class ModelTreeDock : public QDockWidget {
  Q_OBJECT
public:
  explicit ModelTreeDock(QWidget* parent = nullptr);

  void set_scene(std::shared_ptr<scene::Scene> scene);
  void clear();

signals:
  void node_activated(std::uint32_t node_index);
  void node_visibility_changed(std::uint32_t node_index, bool visible);

private:
  QTreeView*          tree_{nullptr};
  QStandardItemModel* model_{nullptr};
  std::shared_ptr<scene::Scene> scene_;
};

} // namespace cadly::ui

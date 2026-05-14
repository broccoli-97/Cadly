#include "cadly/ui/ModelTreeDock.h"

#include <QHeaderView>
#include <QList>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTreeView>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>

namespace cadly::ui {

namespace {
constexpr int kRoleNodeIndex = Qt::UserRole + 1;

QList<QStandardItem*> build_row(const scene::Scene& scn, std::uint32_t idx) {
  const auto& node = scn.nodes[idx];
  auto* name = new QStandardItem(QString::fromStdString(
    node.name.empty() ? std::string("(unnamed)") : node.name));
  name->setData(idx, kRoleNodeIndex);
  name->setEditable(false);

  std::size_t tris = 0;
  if (node.mesh_index && *node.mesh_index < scn.meshes.size() &&
      scn.meshes[*node.mesh_index]) {
    tris = scn.meshes[*node.mesh_index]->triangle_count();
  }
  auto* count = new QStandardItem(QString::number(tris));
  count->setEditable(false);
  count->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

  return {name, count};
}
} // namespace

ModelTreeDock::ModelTreeDock(QWidget* parent)
  : QDockWidget(tr("Model Tree"), parent) {
  setObjectName("ModelTreeDock");
  auto* host = new QWidget(this);
  auto* layout = new QVBoxLayout(host);
  layout->setContentsMargins(0, 0, 0, 0);

  tree_  = new QTreeView(host);
  model_ = new QStandardItemModel(tree_);
  model_->setHorizontalHeaderLabels({tr("Name"), tr("Triangles")});
  tree_->setModel(model_);
  tree_->setUniformRowHeights(true);
  tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
  tree_->header()->setStretchLastSection(false);
  tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

  layout->addWidget(tree_);
  setWidget(host);

  // Activation fires when the user double-clicks or presses enter — we also
  // want a click to update properties, so wire to selectionChanged below.
  connect(tree_->selectionModel(), &QItemSelectionModel::currentChanged, this,
          [this](const QModelIndex& current, const QModelIndex&) {
            if (!current.isValid()) return;
            auto* item = model_->itemFromIndex(model_->index(current.row(), 0,
                                                              current.parent()));
            const auto v = item ? item->data(kRoleNodeIndex) : QVariant();
            if (!v.isValid()) return;
            emit node_activated(v.toUInt());
          });
  // Also relay double-click as activated, for kbd-driven UIs.
  connect(tree_, &QTreeView::activated, this,
          [this](const QModelIndex& idx) {
            auto* item = model_->itemFromIndex(model_->index(idx.row(), 0,
                                                              idx.parent()));
            const auto v = item ? item->data(kRoleNodeIndex) : QVariant();
            if (v.isValid()) emit node_activated(v.toUInt());
          });
}

void ModelTreeDock::clear() {
  model_->removeRows(0, model_->rowCount());
  scene_.reset();
}

void ModelTreeDock::set_scene(std::shared_ptr<scene::Scene> scene) {
  clear();
  scene_ = std::move(scene);
  if (!scene_) return;

  std::function<void(QStandardItem*, std::uint32_t)> populate;
  populate = [&](QStandardItem* parent_name, std::uint32_t node_idx) {
    for (auto c : scene_->nodes[node_idx].children) {
      auto row = build_row(*scene_, c);
      parent_name->appendRow(row);
      populate(row.first(), c);
    }
  };

  for (std::uint32_t i = 0; i < scene_->nodes.size(); ++i) {
    if (scene_->nodes[i].parent == scene::Node::kInvalid) {
      auto row = build_row(*scene_, i);
      model_->invisibleRootItem()->appendRow(row);
      populate(row.first(), i);
    }
  }
  tree_->expandToDepth(1);
}

} // namespace cadly::ui

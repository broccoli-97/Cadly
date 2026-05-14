#include "cadly/ui/PropertiesDock.h"

#include "cadly/scene/Material.h"
#include "cadly/scene/Mesh.h"
#include "cadly/scene/Node.h"

#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>

namespace cadly::ui {

PropertiesDock::PropertiesDock(QWidget* parent)
  : QDockWidget(tr("Properties"), parent) {
  setObjectName("PropertiesDock");

  auto* host = new QWidget(this);
  auto* outer = new QVBoxLayout(host);
  outer->setContentsMargins(8, 8, 8, 8);

  form_ = new QFormLayout();
  form_->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
  form_->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);

  auto make = []() {
    auto* l = new QLabel("—");
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return l;
  };
  lbl_source_    = make();
  lbl_name_      = make();
  lbl_label_     = make();
  lbl_mesh_      = make();
  lbl_material_  = make();
  lbl_triangles_ = make();
  lbl_bounds_    = make();

  form_->addRow(tr("Source"),     lbl_source_);
  form_->addRow(tr("Node"),       lbl_name_);
  form_->addRow(tr("Label"),      lbl_label_);
  form_->addRow(tr("Mesh"),       lbl_mesh_);
  form_->addRow(tr("Material"),   lbl_material_);
  form_->addRow(tr("Triangles"),  lbl_triangles_);
  form_->addRow(tr("Bounds"),     lbl_bounds_);

  outer->addLayout(form_);
  outer->addStretch();
  setWidget(host);
}

void PropertiesDock::set_scene(std::shared_ptr<scene::Scene> scene) {
  scene_ = std::move(scene);
  if (scene_) {
    lbl_source_->setText(QString::fromStdString(scene_->source_file.string()));
  }
  clear();
}

void PropertiesDock::clear() {
  lbl_name_->setText("—");
  lbl_label_->setText("—");
  lbl_mesh_->setText("—");
  lbl_material_->setText("—");
  lbl_triangles_->setText("—");
  lbl_bounds_->setText("—");
}

void PropertiesDock::show_node(std::uint32_t node_index) {
  if (!scene_ || node_index >= scene_->nodes.size()) { clear(); return; }
  const auto& n = scene_->nodes[node_index];
  lbl_name_->setText(QString::fromStdString(n.name));
  lbl_label_->setText(QString::fromStdString(n.source_label));

  if (n.mesh_index && *n.mesh_index < scene_->meshes.size()) {
    const auto& mesh = *scene_->meshes[*n.mesh_index];
    lbl_mesh_->setText(QString::fromStdString(mesh.name));
    lbl_triangles_->setText(QString::number(mesh.triangle_count()));
    if (n.material_override && *n.material_override < scene_->materials.size()) {
      lbl_material_->setText(QString::fromStdString(
          scene_->materials[*n.material_override].name));
    } else if (!mesh.submeshes.empty()) {
      const auto idx = mesh.submeshes.front().material_index;
      if (idx < scene_->materials.size()) {
        lbl_material_->setText(QString::fromStdString(
            scene_->materials[idx].name));
      }
    }
  }

  if (n.world_bounds.valid()) {
    const auto e = n.world_bounds.extent();
    lbl_bounds_->setText(QString("%1 × %2 × %3")
      .arg(e.x, 0, 'f', 2)
      .arg(e.y, 0, 'f', 2)
      .arg(e.z, 0, 'f', 2));
  } else {
    lbl_bounds_->setText("—");
  }
}

} // namespace cadly::ui

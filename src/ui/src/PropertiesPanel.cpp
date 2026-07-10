#include "PropertiesPanel.h"

#include "IconUtils.h"

#include "cadly/scene/Material.h"
#include "cadly/scene/Mesh.h"
#include "cadly/scene/Node.h"

#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace cadly::ui {

PropertiesPanel::PropertiesPanel(QWidget* parent) : QWidget(parent) {
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(12, 10, 12, 10);

  auto* form = new QFormLayout();
  form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
  form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
  form->setHorizontalSpacing(10);
  form->setVerticalSpacing(6);

  auto make = [this]() {
    auto* l = new QLabel(QStringLiteral("—"), this);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setWordWrap(true);
    return l;
  };
  lbl_source_    = make();
  lbl_name_      = make();
  lbl_label_     = make();
  lbl_mesh_      = make();
  lbl_material_  = make();
  lbl_triangles_ = make();
  lbl_bounds_    = make();
  lbl_triangles_->setFont(mono_font(12));
  lbl_bounds_->setFont(mono_font(12));

  form->addRow(tr("Source"),    lbl_source_);
  form->addRow(tr("Node"),      lbl_name_);
  form->addRow(tr("Label"),     lbl_label_);
  form->addRow(tr("Mesh"),      lbl_mesh_);
  form->addRow(tr("Material"),  lbl_material_);
  form->addRow(tr("Triangles"), lbl_triangles_);
  form->addRow(tr("Bounds"),    lbl_bounds_);

  outer->addLayout(form);
  outer->addStretch();
}

void PropertiesPanel::set_scene(std::shared_ptr<scene::Scene> scene) {
  scene_ = std::move(scene);
  if (scene_) {
    lbl_source_->setText(QString::fromStdString(scene_->source_file.string()));
  } else {
    lbl_source_->setText(QStringLiteral("—"));
  }
  clear();
}

void PropertiesPanel::clear() {
  for (auto* l : {lbl_name_, lbl_label_, lbl_mesh_, lbl_material_,
                  lbl_triangles_, lbl_bounds_}) {
    l->setText(QStringLiteral("—"));
  }
}

void PropertiesPanel::show_node(std::uint32_t node_index) {
  if (!scene_ || node_index >= scene_->nodes.size()) {
    clear();
    return;
  }
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
    lbl_bounds_->setText(QStringLiteral("%1 × %2 × %3")
      .arg(e.x, 0, 'f', 2)
      .arg(e.y, 0, 'f', 2)
      .arg(e.z, 0, 'f', 2));
  } else {
    lbl_bounds_->setText(QStringLiteral("—"));
  }
}

} // namespace cadly::ui

#include "cadly/ui/ImportOptionsDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QVBoxLayout>

namespace cadly::ui {

ImportOptionsDialog::ImportOptionsDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("Import Options"));
  setMinimumWidth(360);

  auto* outer = new QVBoxLayout(this);

  auto* mesh_box = new QGroupBox(tr("Tessellation"), this);
  auto* mesh_form = new QFormLayout(mesh_box);

  linear_ = new QDoubleSpinBox(this);
  linear_->setRange(0.0001, 100.0);
  linear_->setDecimals(4);
  linear_->setSingleStep(0.1);

  angular_ = new QDoubleSpinBox(this);
  angular_->setRange(0.001, 2.0);
  angular_->setDecimals(3);
  angular_->setSingleStep(0.05);
  angular_->setSuffix(" rad");

  relative_ = new QCheckBox(tr("Relative deflection"), this);
  parallel_ = new QCheckBox(tr("Parallel meshing"), this);

  mesh_form->addRow(tr("Linear deflection"),  linear_);
  mesh_form->addRow(tr("Angular deflection"), angular_);
  mesh_form->addRow(relative_);
  mesh_form->addRow(parallel_);

  auto* meta_box = new QGroupBox(tr("Metadata"), this);
  auto* meta_form = new QFormLayout(meta_box);
  load_colors_     = new QCheckBox(tr("Load colours"), this);
  load_names_      = new QCheckBox(tr("Load names"), this);
  load_hierarchy_  = new QCheckBox(tr("Load assembly hierarchy"), this);
  meta_form->addRow(load_colors_);
  meta_form->addRow(load_names_);
  meta_form->addRow(load_hierarchy_);

  outer->addWidget(mesh_box);
  outer->addWidget(meta_box);

  auto* buttons = new QDialogButtonBox(
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  outer->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  set_options(cad::ImportOptions{});
}

cad::ImportOptions ImportOptionsDialog::options() const {
  cad::ImportOptions o;
  o.linear_deflection   = linear_->value();
  o.angular_deflection  = angular_->value();
  o.relative_deflection = relative_->isChecked();
  o.parallel_meshing    = parallel_->isChecked();
  o.load_colors         = load_colors_->isChecked();
  o.load_names          = load_names_->isChecked();
  o.load_hierarchy      = load_hierarchy_->isChecked();
  return o;
}

void ImportOptionsDialog::set_options(const cad::ImportOptions& o) {
  linear_->setValue(o.linear_deflection);
  angular_->setValue(o.angular_deflection);
  relative_->setChecked(o.relative_deflection);
  parallel_->setChecked(o.parallel_meshing);
  load_colors_->setChecked(o.load_colors);
  load_names_->setChecked(o.load_names);
  load_hierarchy_->setChecked(o.load_hierarchy);
}

} // namespace cadly::ui

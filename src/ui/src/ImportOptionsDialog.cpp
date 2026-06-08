#include "cadly/ui/ImportOptionsDialog.h"

#include <QCheckBox>
#include <QComboBox>
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

  mode_ = new QComboBox(this);
  mode_->addItem(tr("Visual-relative"), static_cast<int>(cad::TessellationMode::VisualRelative));
  mode_->addItem(tr("Absolute"), static_cast<int>(cad::TessellationMode::Absolute));

  linear_ = new QDoubleSpinBox(this);
  linear_->setRange(0.0001, 100.0);
  linear_->setDecimals(4);
  linear_->setSingleStep(0.1);

  angular_ = new QDoubleSpinBox(this);
  angular_->setRange(0.001, 2.0);
  angular_->setDecimals(3);
  angular_->setSingleStep(0.05);
  angular_->setSuffix(" rad");

  target_px_ = new QDoubleSpinBox(this);
  target_px_->setRange(0.05, 10.0);
  target_px_->setDecimals(2);
  target_px_->setSingleStep(0.25);
  target_px_->setSuffix(" px");

  reference_px_ = new QDoubleSpinBox(this);
  reference_px_->setRange(100.0, 10000.0);
  reference_px_->setDecimals(0);
  reference_px_->setSingleStep(250.0);

  min_deflection_ = new QDoubleSpinBox(this);
  min_deflection_->setRange(0.000001, 100.0);
  min_deflection_->setDecimals(6);
  min_deflection_->setSingleStep(0.01);

  max_relative_ = new QDoubleSpinBox(this);
  max_relative_->setRange(0.000001, 0.1);
  max_relative_->setDecimals(6);
  max_relative_->setSingleStep(0.0001);

  relative_ = new QCheckBox(tr("Relative deflection"), this);
  parallel_ = new QCheckBox(tr("Parallel meshing"), this);

  mesh_form->addRow(tr("Mode"), mode_);
  mesh_form->addRow(tr("Linear deflection"),  linear_);
  mesh_form->addRow(tr("Angular deflection"), angular_);
  mesh_form->addRow(tr("Target error"), target_px_);
  mesh_form->addRow(tr("Reference pixels"), reference_px_);
  mesh_form->addRow(tr("Min deflection"), min_deflection_);
  mesh_form->addRow(tr("Max relative"), max_relative_);
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
  connect(mode_, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
          this, [this](int) {
    const auto mode = static_cast<cad::TessellationMode>(
      mode_->currentData().toInt());
    const bool visual = mode == cad::TessellationMode::VisualRelative;
    target_px_->setEnabled(visual);
    reference_px_->setEnabled(visual);
    min_deflection_->setEnabled(visual);
    max_relative_->setEnabled(visual);
    relative_->setEnabled(mode == cad::TessellationMode::Absolute);
  });

  set_options(cad::ImportOptions{});
}

cad::ImportOptions ImportOptionsDialog::options() const {
  cad::ImportOptions o;
  o.tessellation_mode = static_cast<cad::TessellationMode>(
    mode_->currentData().toInt());
  o.linear_deflection   = linear_->value();
  o.angular_deflection  = angular_->value();
  o.target_screen_error_px = target_px_->value();
  o.reference_screen_pixels = reference_px_->value();
  o.min_linear_deflection = min_deflection_->value();
  o.max_relative_deflection = max_relative_->value();
  o.relative_deflection = relative_->isChecked();
  o.parallel_meshing    = parallel_->isChecked();
  o.load_colors         = load_colors_->isChecked();
  o.load_names          = load_names_->isChecked();
  o.load_hierarchy      = load_hierarchy_->isChecked();
  return o;
}

void ImportOptionsDialog::set_options(const cad::ImportOptions& o) {
  const int mode_index = mode_->findData(static_cast<int>(o.tessellation_mode));
  mode_->setCurrentIndex(mode_index >= 0 ? mode_index : 0);
  linear_->setValue(o.linear_deflection);
  angular_->setValue(o.angular_deflection);
  target_px_->setValue(o.target_screen_error_px);
  reference_px_->setValue(o.reference_screen_pixels);
  min_deflection_->setValue(o.min_linear_deflection);
  max_relative_->setValue(o.max_relative_deflection);
  relative_->setChecked(o.relative_deflection);
  parallel_->setChecked(o.parallel_meshing);
  load_colors_->setChecked(o.load_colors);
  load_names_->setChecked(o.load_names);
  load_hierarchy_->setChecked(o.load_hierarchy);
  const bool visual = o.tessellation_mode == cad::TessellationMode::VisualRelative;
  target_px_->setEnabled(visual);
  reference_px_->setEnabled(visual);
  min_deflection_->setEnabled(visual);
  max_relative_->setEnabled(visual);
  relative_->setEnabled(!visual);
}

} // namespace cadly::ui

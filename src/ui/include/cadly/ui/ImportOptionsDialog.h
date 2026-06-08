#pragma once

#include "cadly/cad/ICadImporter.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;

namespace cadly::ui {

// Modal dialog for tweaking BRepMesh deflection and metadata loading flags.
// The host shows this before the import job starts and reads `options()` when
// the user accepts.
class ImportOptionsDialog : public QDialog {
  Q_OBJECT
public:
  explicit ImportOptionsDialog(QWidget* parent = nullptr);

  cad::ImportOptions options() const;
  void               set_options(const cad::ImportOptions& opts);

private:
  QComboBox*      mode_{nullptr};
  QDoubleSpinBox* linear_{nullptr};
  QDoubleSpinBox* angular_{nullptr};
  QDoubleSpinBox* target_px_{nullptr};
  QDoubleSpinBox* reference_px_{nullptr};
  QDoubleSpinBox* min_deflection_{nullptr};
  QDoubleSpinBox* max_relative_{nullptr};
  QCheckBox*      relative_{nullptr};
  QCheckBox*      parallel_{nullptr};
  QCheckBox*      load_colors_{nullptr};
  QCheckBox*      load_names_{nullptr};
  QCheckBox*      load_hierarchy_{nullptr};
};

} // namespace cadly::ui

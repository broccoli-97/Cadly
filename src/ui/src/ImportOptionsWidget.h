#pragma once

// The import options form, lifted out of the old every-open modal dialog so
// it can live persistently in the inspector's Import tab AND inside the
// opt-in pre-flight dialog without duplication. Private to the ui module.

#include "cadly/cad/ICadImporter.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;

namespace cadly::ui {

class ImportOptionsWidget : public QWidget {
  Q_OBJECT
public:
  explicit ImportOptionsWidget(QWidget* parent = nullptr);

  cad::ImportOptions options() const;
  void set_options(const cad::ImportOptions& o);

signals:
  // Any control changed through the UI (not via set_options).
  void options_edited();

private:
  void update_enablement();

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
  bool            applying_{false};
};

} // namespace cadly::ui
